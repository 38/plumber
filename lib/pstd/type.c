/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <error.h>
#include <utils/static_assertion.h>

#include <pservlet.h>
#include <proto.h>

#include <pstd/type.h>

/**
 * @brief The internal alias for the pstd_typeinfo_accessor_t
 **/
typedef pstd_type_accessor_t _acc_t;

/**
 * @brief Represent a accessor data
 * @note  Because we allows type variables, so that we actually do not
 *        query the type info util the type callback is called
 **/
typedef struct _accessor_t {
	uint32_t            init:1;         /*!< If this accessor is initialized */
	char*               field;          /*!< The field expression we want to query */
	pipe_t              pipe;           /*!< The target pipe */
	uint32_t            offset;         /*!< The begining of the interested memory region */
	uint32_t            size;           /*!< The size of the interested memory region */
	struct _accessor_t* next;           /*!< The next accessor for the same pipe */
} _accessor_t;

/**
 * @brief The type assertion object
 **/
typedef struct _type_assertion_t {
	pstd_type_assertion_t       func; /*!< The assertion function */
	const void*                 data; /*!< The additional data for the assertion function */
	struct _type_assertion_t*   next; /*!< The next pointer */
} _type_assertion_t;

/**
 * @brief The type const object
 **/
typedef struct _const_t {
	char*    field;      /*!< The field name for the const */
	uint32_t is_real:1;  /*!< If this is the real number */
	uint32_t is_signed:1;/*!< If this is a signed value */
	uint32_t size;       /*!< The size of this value */
	void*    target;     /*!< The buffer for this value */
	struct _const_t* next;  /*!< The next const definition */
} _const_t;

/**
 * @brief Represent the type information for one pipe
 **/
typedef struct {
	uint32_t                cb_setup:1;    /*!< Indicates if we have already installed the type callback for this type info */
	uint32_t                init:1;        /*!< If the type info has been initialized */
	char*                   name;          /*!< The name of the type */
	uint32_t                full_size;     /*!< The size of the header section */
	uint32_t                used_size;     /*!< The size of the header data we actually used */
	size_t                  buf_begin;     /*!< The offest for buffer of the type context isntance for this pipe begins */
	_accessor_t*            accessor_list; /*!< The list of accessors related to this pipe */
	_const_t*               const_list;    /*!< The list of the constant defined by this pipe */
	_type_assertion_t*      assertion_list;/*!< The assertion list */
} _typeinfo_t;

/**
 * @brief The type context for a servlet instance
 **/
struct _pstd_type_model_t {
	uint32_t                     pipe_cap;    /*!< The pipe info array capacity */
	runtime_api_pipe_id_t        pipe_max;    /*!< The upper bound of the pipe id */
	_typeinfo_t*                 type_info;   /*!< The type information */
	_acc_t                       accessor_cap;/*!< The capacity of the accessor table */
	_acc_t                       accessor_cnt;/*!< The size of the accessor table */
	_accessor_t*                 accessor;    /*!< The accessor table */
};

/**
 * @brief The pipe header
 **/
typedef struct __attribute__((packed)) {
	size_t valid_size;   /*!< The valid size */
	char   data[0];
} _header_buf_t;

/**
 * @brief The type context instance
 **/
struct _pstd_type_instance_t {
	uint32_t                    heapmem:1;   /*!< Indicates if this instance uses the heap memory */
	const pstd_type_model_t*    model;       /*!< The underlying type model */
	uintptr_t __padding__[0];
	char                        buffer[0];   /*!< The actual buffer memory */
};

/**
 * @brief Output the libproto error information in the log
 * @param err The error object
 * @return nothing
 **/
static inline void _proto_err_stack(const proto_err_t* err)
{
#ifdef LOG_ERROR_ENABLED
	char buffer[128];

	if(NULL == err && NULL == (err = proto_err_stack())) return;

	LOG_ERROR("libproto error: %s", proto_err_str(err, buffer, sizeof(buffer)));
	if(NULL != err->child)
	    _proto_err_stack(err->child);
#else /* LOG_ERROR_ENABLED */
	(void)err;
#endif /* LOG_ERROR_ENABLED */
}

/**
 * @brief The callback function that fill up the type related data when the type is determined by
 *        the framework
 * @param pipe which pipe we are talking about
 * @param typename the name of the actual type of the pipe
 * @param data the related typeinfo object
 * @return status code
 **/
static int _on_pipe_type_determined(pipe_t pipe, const char* typename, void* data)
{
	pstd_type_model_t* model = (pstd_type_model_t*)data;
	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(pipe);

	/* Duplicate the typename */
	size_t namelen = strlen(typename) + 1;
	if(NULL == (typeinfo->name = (char*)malloc(namelen)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the type name");

	memcpy(typeinfo->name, typename, namelen);

	int rc  = ERROR_CODE(int);

	if(ERROR_CODE(int) == proto_init())
	    ERROR_LOG_GOTO(ERR, "Cannot initialize libproto");

	/* Get the full size of the header */
	if(ERROR_CODE(uint32_t) == (typeinfo->full_size = proto_db_type_size(typename)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the full size of type %s", typename);


	/* Check all the assertions */
	_type_assertion_t* assertion;
	for(assertion = typeinfo->assertion_list; NULL != assertion; assertion = assertion->next)
	    if(ERROR_CODE(int) == assertion->func(pipe, typename, assertion->data))
	        ERROR_LOG_GOTO(ERR, "Type assertion failed");

	/* Then we need to fetch all the constants */
	_const_t* constant;
	for(constant = typeinfo->const_list; NULL != constant; constant = constant->next)
	{
		proto_db_field_prop_t prop = proto_db_field_type_info(typename, constant->field);
		if(ERROR_CODE(int) == prop)
		    ERROR_LOG_GOTO(ERR, "Cannot query the field type property");

		if(!(prop & PROTO_DB_FIELD_PROP_NUMERIC))
		    ERROR_LOG_GOTO(ERR, "Type error: numeric type expected for a constant");

		const void* data;
		size_t size;

		if(1 != proto_db_field_get_default(typename, constant->field, &data, &size))
		    ERROR_LOG_GOTO(ERR, "Cannot get the default value of the field");

		if(!(prop & PROTO_DB_FIELD_PROP_REAL))
		{
			/* This is an interger value */
			if(constant->is_real) ERROR_LOG_GOTO(ERR, "Type error: integer value expected, but floating point number got");
			if(((prop & PROTO_DB_FIELD_PROP_SIGNED) != 0) ^ constant->is_signed)
			    ERROR_LOG_GOTO(ERR, "Type error: signedness mismatch");
			if(size  > constant->size)
			    ERROR_LOG_GOTO(ERR, "Type error: the integer constant has been truncated");
			memcpy(constant->target, data, size);
			uint8_t* u8 = (uint8_t*)constant->target;
			/* If this is a signed value, we should expand the sign bit */
			if(constant->is_signed && (u8[size - 1]&0x80)) u8[size - 1] &= 0x7f, u8[constant->size - 1] |= 0x80;
		}
		else
		{
			/* This is a real number */
			if(!constant->is_real) ERROR_LOG_GOTO(ERR, "Type error: floating point value expected, but integer number got");
			if(size == 4 && constant->size == 4) *(float*)constant->target = *(float*)data;
			if(size == 4 && constant->size == 8) *(double*)constant->target = *(float*)data;
			if(size == 8 && constant->size == 4) *(float*)constant->target = (float)*(double*)data;
			if(size == 8 && constant->size == 8) *(double*)constant->target = *(double*)data;
		}
	}

	/* Fill the offset info into accessors */
	_accessor_t* accessor;
	for(accessor = typeinfo->accessor_list; NULL != accessor; accessor = accessor->next)
	{
		if(ERROR_CODE(uint32_t) == (accessor->offset = proto_db_type_offset(typename, accessor->field, &accessor->size)))
		    ERROR_LOG_GOTO(ERR, "Cannot get the type param for %s.%s", typename, accessor->field);
		accessor->init = 1;

		if(typeinfo->used_size < accessor->offset + accessor->size)
		    typeinfo->used_size = accessor->offset + accessor->size;
	}

	/* Update the buffer offest information.
	 * Yes, at this point the approach is not optimal, we spend O(N^2) time to update the buffer offset.
	 * But it's Ok, because we are actually in initialization pharse */
	if(typeinfo->used_size > 0)
	{
		runtime_api_pipe_id_t i;
		for(i = (runtime_api_pipe_id_t)(PIPE_GET_ID(pipe) + 1); i < model->pipe_max; i ++)
		    model->type_info[i].buf_begin += typeinfo->used_size + sizeof(_header_buf_t);
	}

	typeinfo->init = 1u;

	rc = 0;

ERR:

	/* Because even though it's error case, but the memory allocated for the type name is assigned to
	 * typeinfo->name, and will be disposed when the type context is getting disposed. So we do not
	 * need to do anything about the name buffer at this point */

	if(ERROR_CODE(int) == proto_finalize())
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot finalize libproto");
	}

	if(ERROR_CODE(int) == rc)
	{
		LOG_ERROR("===========libproto error stack============");
		_proto_err_stack(NULL);
		LOG_ERROR("===========================================");
	}
	return rc;
}

/**
 * @brief Ensure the type_info array have a slot for the given pipe
 * @param ctx The type context
 * @param pipe The pipe descriptor
 * @return status code
 **/
static inline int _ensure_pipe_typeinfo(pstd_type_model_t* ctx, pipe_t pipe)
{
	runtime_api_pipe_id_t pid = PIPE_GET_ID(pipe);

	if(ctx->pipe_cap <= pid + 1u)
	{
		_typeinfo_t* newbuf = (_typeinfo_t*)realloc(ctx->type_info, sizeof(ctx->type_info[0]) * ctx->pipe_cap * 2);
		if(NULL == newbuf)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the type info array");

		memset(newbuf + sizeof(ctx->type_info[0]) * ctx->pipe_cap, 0,  sizeof(ctx->type_info[0]) * ctx->pipe_cap);

		ctx->pipe_cap <<= 1u;
		ctx->type_info = newbuf;
	}

	/* We don't need to update the buf_begin field, because we current don't know the size, and all
	 * the offset are 0 */
	if(ctx->pipe_max < pid + 1u)
	    ctx->pipe_max = (runtime_api_pipe_id_t)(pid + 1u);

	if(!ctx->type_info[pid].cb_setup)
	{
		if(ERROR_CODE(int) == pipe_set_type_callback(pipe, _on_pipe_type_determined, ctx))
		    ERROR_RETURN_LOG(int, "Cannot setup the type callback function for the pipe");

		ctx->type_info[pid].cb_setup = 1;
	}

	return 0;
}

/**
 * @brief Allocate a new type accessor in the given context
 * @param ctx The context
 * @param pipe The pipe owns this accessor
 * @param field_expr The field expression
 * @return The accessor id or error code
 **/
static inline _acc_t _accessor_alloc(pstd_type_model_t* ctx, pipe_t pipe, const char* field_expr)
{
	if(ctx->accessor_cap <= ctx->accessor_cnt)
	{
		_accessor_t* newbuf = (_accessor_t*)realloc(ctx->accessor, sizeof(ctx->accessor[0]) * ctx->accessor_cap * 2);
		if(NULL == newbuf)
		    ERROR_RETURN_LOG_ERRNO(_acc_t, "Cannot resize the accessor array");

		ctx->accessor_cap *= 2;
		ctx->accessor = newbuf;
	}

	_accessor_t* accessor = ctx->accessor + ctx->accessor_cnt;

	accessor->init = 0;

	size_t len = strlen(field_expr) + 1;

	if(NULL == (accessor->field = (char*)malloc(len)))
	    ERROR_RETURN_LOG_ERRNO(_acc_t, "Cannot allocate the field expression buffer");

	memcpy(accessor->field, field_expr, len);

	accessor->pipe = pipe;

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(ctx, pipe))
	    ERROR_RETURN_LOG_ERRNO(_acc_t, "Cannot resize the typeinfo array");

	accessor->next = ctx->type_info[PIPE_GET_ID(pipe)].accessor_list;
	ctx->type_info[PIPE_GET_ID(pipe)].accessor_list = accessor;

	return ctx->accessor_cnt ++;
}

pstd_type_model_t* pstd_type_model_new()
{
	pstd_type_model_t* ret = (pstd_type_model_t*)calloc(1, sizeof(pstd_type_model_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the type model");

	ret->pipe_max = 0;
	ret->pipe_cap = 32;       /*! TODO: magic number */

	if(NULL == (ret->type_info = (_typeinfo_t*)calloc(ret->pipe_cap, sizeof(ret->type_info[0]))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for for the typeinfo array");

	ret->accessor_cnt = 0;
	ret->accessor_cap = 32;   /*!< TODO: magic number */
	if(NULL == (ret->accessor = (_accessor_t*)malloc(ret->accessor_cap * sizeof(ret->accessor[0]))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the accessor array");

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->type_info) free(ret->type_info);
		if(NULL != ret->accessor)  free(ret->accessor);
	}

	return NULL;
}

int pstd_type_model_free(pstd_type_model_t* model)
{
	if(NULL == model)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(model->accessor != NULL)
	{
		_acc_t i;
		for(i = 0; i < model->accessor_cnt; i ++)
		{
			if(NULL != model->accessor[i].field)
			    free(model->accessor[i].field);
		}

		free(model->accessor);
	}

	if(model->type_info != NULL)
	{
		runtime_api_pipe_id_t i;
		for(i = 0; i < model->pipe_max; i ++)
		{
			_type_assertion_t* ptr;
			for(ptr = model->type_info[i].assertion_list; NULL != ptr;)
			{
				_type_assertion_t* cur = ptr;
				ptr = ptr->next;

				free(cur);
			}

			_const_t* con;
			for(con = model->type_info[i].const_list; NULL != con;)
			{
				_const_t* cur = con;
				con = con->next;
				free(cur);
			}

			if(NULL != model->type_info[i].name)
			    free(model->type_info[i].name);
		}

		/* We do not need to dispose the accessor list, because we do not hold the ownership of the accessors */

		free(model->type_info);
	}

	free(model);

	return 0;
}

pstd_type_accessor_t pstd_type_model_get_accessor(pstd_type_model_t* model, pipe_t pipe, const char* field_expr)
{
	if(NULL == model || NULL == field_expr || RUNTIME_API_PIPE_IS_VIRTUAL(pipe) || ERROR_CODE(pipe_t) == pipe)
	    ERROR_RETURN_LOG(pstd_type_accessor_t, "Invalid arguments");

	return _accessor_alloc(model, pipe, field_expr);
}

int pstd_type_model_assert(pstd_type_model_t* model, pipe_t pipe, pstd_type_assertion_t assertion, const void* data)
{
	if(NULL == model || NULL == assertion || ERROR_CODE(pipe_t) == pipe || RUNTIME_API_PIPE_IS_VIRTUAL(pipe))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(model, pipe))
	    ERROR_RETURN_LOG(int, "Cannot resize the typeinfo array");

	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(pipe);

	_type_assertion_t* obj = (_type_assertion_t*)malloc(sizeof(*assertion));
	if(NULL == obj)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the type assertion object");

	obj->func = assertion;
	obj->data = data;
	obj->next = typeinfo->assertion_list;
	typeinfo->assertion_list = obj;

	return 0;
}

int pstd_type_model_const(pstd_type_model_t* model, pipe_t pipe, const char* field, int is_signed, int is_real, void* buf, uint32_t bufsize)
{
	if(NULL == model || NULL == field || ERROR_CODE(pipe_t) == pipe || RUNTIME_API_PIPE_IS_VIRTUAL(pipe))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(model, pipe))
	    ERROR_RETURN_LOG(int, "Cannot resize the typeinfo arrray");

	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(pipe);

	_const_t* obj = (_const_t*)malloc(sizeof(*obj));
	if(NULL == obj)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the constant object");

	obj->field = NULL;
	obj->is_signed = (is_signed != 0);
	obj->is_real   = (is_real != 0);
	obj->target    = buf;
	obj->size      = bufsize;
	if(NULL == (obj->field = strdup(field)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the field string");
	obj->next = typeinfo->const_list;
	typeinfo->const_list = obj;

	return 0;

ERR:
	if(obj->field != NULL) free(obj->field);
	free(obj);
	return ERROR_CODE(int);
}

/**
 * @brief Compute the instance buffer size
 * @param model the type model
 * @return size
 **/
static inline size_t _inst_buf_size(const pstd_type_model_t* model)
{
	if(model->pipe_max == 0) return 0;

	runtime_api_pipe_id_t last = (runtime_api_pipe_id_t)(model->pipe_max - 1);

	return (size_t)(model->type_info[last].buf_begin + model->type_info[last].used_size +
	                                                   (model->type_info[last].used_size > 0 ? sizeof(_header_buf_t) : 0));
}

size_t pstd_type_instance_size(const pstd_type_model_t* model)
{
	if(NULL == model)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	return sizeof(pstd_type_instance_t) + _inst_buf_size(model);
}

pstd_type_instance_t* pstd_type_instance_new(const pstd_type_model_t* model, void* mem)
{
	if(NULL == model)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t size = sizeof(pstd_type_instance_t) + _inst_buf_size(model);
	pstd_type_instance_t* ret = (pstd_type_instance_t*)mem;

	if(NULL == ret && NULL == (ret = (pstd_type_instance_t*)malloc(size)))
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the type context instance");

	ret->heapmem = (mem == NULL);
	ret->model = model;

	runtime_api_pipe_id_t i;
	for(i = 0; i < model->pipe_max; i ++)
	    if(model->type_info[i].used_size > 0)
	        *(size_t*)(ret->buffer + model->type_info[i].buf_begin) = 0;

	return ret;
}

int pstd_type_instance_free(pstd_type_instance_t* inst)
{
	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_api_pipe_id_t i;
	int rc = 0;
	for(i = 0; i < inst->model->pipe_max; i ++)
	{

		if(!inst->model->type_info[i].init || inst->model->type_info[i].used_size == 0) continue;

		runtime_api_pipe_flags_t flags = PIPE_INPUT;

		if(ERROR_CODE(int) == pipe_cntl(RUNTIME_API_PIPE_FROM_ID(i), PIPE_CNTL_GET_FLAGS, &flags))
		{
			LOG_ERROR("Cannot get the pipe flag");
			rc = ERROR_CODE(int);
		}

		if(PIPE_FLAGS_IS_WRITABLE(flags))
		{
			const _header_buf_t* buf = (const _header_buf_t*)(inst->buffer + inst->model->type_info[i].buf_begin);
			const char* data = buf->data;
			size_t bytes_to_write = buf->valid_size;
			while(bytes_to_write > 0)
			{
				size_t bytes_written = pipe_hdr_write(RUNTIME_API_PIPE_FROM_ID(i), data, bytes_to_write);
				if(ERROR_CODE(size_t) == bytes_written)
				{
					LOG_ERROR("Cannot write header to the pipe, bytes remaining: %zu", bytes_to_write);
					rc = ERROR_CODE(int);
					break;
				}
				bytes_to_write -= bytes_written;
				data += bytes_written;
			}
		}
	}

	if(inst->heapmem)
	    free(inst);
	return rc;
}
/**
 * @brief Ensure we have read the nbytes-th bytes in the header
 * @param inst the type context instance
 * @param accessor the accessor object
 * @param nbytes how many bytes we need to ensure
 * @return status code
 **/
static inline int _ensure_header_read(pstd_type_instance_t* inst, const _accessor_t* accessor, size_t nbytes)
{
	const _typeinfo_t* typeinfo = inst->model->type_info + PIPE_GET_ID(accessor->pipe);
	_header_buf_t* buffer = (_header_buf_t*)(inst->buffer + typeinfo->buf_begin);
	size_t bytes_can_read = typeinfo->used_size - buffer->valid_size;

	while(buffer->valid_size < nbytes)
	{
		size_t rc = pipe_hdr_read(accessor->pipe, buffer->data + buffer->valid_size, bytes_can_read);
		if(ERROR_CODE(size_t) == rc)
		    ERROR_RETURN_LOG(int, "Cannot read header");

		if(rc == 0)
		{
			int eof_rc = pipe_eof(accessor->pipe);
			if(ERROR_CODE(int) == eof_rc)
			    ERROR_RETURN_LOG(int, "pipe_eof returns an error");

			if(eof_rc)
			{
				if(buffer->valid_size > 0)
				    ERROR_RETURN_LOG(int, "Unexpected end of data stream");
				else /* We are ok with the zero input */
				    return 0;
			}
		}

		bytes_can_read -= rc;
		buffer->valid_size += rc;
	}

	return 0;
}

size_t pstd_type_instance_field_size(pstd_type_instance_t* inst, pstd_type_accessor_t accessor)
{
	if(NULL == inst || ERROR_CODE(pstd_type_accessor_t) == accessor || accessor >= inst->model->accessor_cnt)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const _accessor_t* obj = inst->model->accessor + accessor;

	return obj->init ? obj->size : 0;
}

size_t pstd_type_instance_read(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, void* buf, size_t bufsize)
{
	if(NULL == inst || ERROR_CODE(pstd_type_accessor_t) == accessor || NULL == buf || accessor >= inst->model->accessor_cnt)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const _accessor_t* obj = inst->model->accessor + accessor;

	/* It's possible the pipe is unassigned in the graph, so that we do not have any type info at this point
	 * In this case, we must stop at this point */
	if(!inst->model->accessor[accessor].init)
	    return 0;

	if(bufsize > obj->size) bufsize = obj->size;

	if(bufsize == 0) return 0;

	if(ERROR_CODE(int) == _ensure_header_read(inst, obj, obj->offset + bufsize))
	    ERROR_RETURN_LOG(size_t, "Cannot ensure the header buffer is valid");

	const _header_buf_t* buffer = (const _header_buf_t*)(inst->buffer + inst->model->type_info[PIPE_GET_ID(obj->pipe)].buf_begin);

	if(buffer->valid_size > 0)
	    memcpy(buf, buffer->data + obj->offset, bufsize);
	else
	    return 0;

	return bufsize;
}

static inline int _ensure_header_write(pstd_type_instance_t* inst, const _accessor_t* accessor, size_t nbytes)
{
	const _typeinfo_t* typeinfo = inst->model->type_info + PIPE_GET_ID(accessor->pipe);
	_header_buf_t* buffer = (_header_buf_t*)(inst->buffer + typeinfo->buf_begin);
	if(nbytes <= buffer->valid_size) return 0;

	size_t bytes_to_fill = nbytes - buffer->valid_size;

	memset(buffer->data + buffer->valid_size, 0, bytes_to_fill);

	buffer->valid_size = nbytes;

	return 0;
}

int pstd_type_instance_write(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, const void* buf, size_t bufsize)
{
	if(NULL == inst || ERROR_CODE(pstd_type_accessor_t) == accessor || NULL == buf || accessor >= inst->model->accessor_cnt)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _accessor_t* obj = inst->model->accessor + accessor;

	if(!obj->init) return 0;

	if(bufsize > obj->size) bufsize = obj->size;

	if(bufsize == 0) return 0;

	if(ERROR_CODE(int) == _ensure_header_write(inst, obj, obj->offset + bufsize))
	    ERROR_RETURN_LOG(int, "Cannot ensure the header buffer is valid");

	_header_buf_t* buffer = (_header_buf_t*)(inst->buffer + inst->model->type_info[PIPE_GET_ID(obj->pipe)].buf_begin);
	memcpy(buffer->data + obj->offset, buf, bufsize);

	return 0;
}
