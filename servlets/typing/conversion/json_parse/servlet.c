/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <json.h>

#include <utils/static_assertion.h>

#include <pstd.h>
#include <pstd/types/string.h>
#include <pservlet.h>
#include <proto.h>

/**
 * @brief the operation we should perform
 **/
typedef enum {
	OPEN,     /*!< This means we should open an object for write */
	OPEN_SUBS,/*!< This means we want to open a subscription */
	CLOSE,    /*!< This means we should close the object because nothing to write */
	WRITE     /*!< We need to write the primitive to the type */
} opcode_t;

/**
 * @brief The object mapping operations
 **/
typedef struct {
	opcode_t    opcode;        /*!< The operation code */
	char*       field;         /*!< Only used for opening a field: The field name we need to open */
	uint32_t    index;         /*!< The index used when we are opening an array list */
	pstd_type_accessor_t acc;  /*!< Only used for primitive field: The accessor we should use */
	size_t               size; /*!< Only used for primitive: The size of the data field */
	enum {
		TYPE_SIGNED,              /*!< This is an integer */
		TYPE_UNSIGNED,            /*!< This is a unsigned integer */
		TYPE_FLOAT,               /*!< THis is a float point number */
		TYPE_STRING               /*!< This is a string */
	}                    type; /*!< Only used for primitive: The type of this data field */
} oper_t;

/**
 * @brief The output spec for each output ports
 **/
typedef struct {
	pipe_t         pipe;       /*!< The pipe we want to produce the contents */
	char*          name;       /*!< The name of the pipe */
	uint32_t       cap;        /*!< The capacity of the operation array */
	uint32_t       nops;       /*!< The number of operations we need to be done for this type */
	oper_t*        ops;        /*!< The operations we need to dump the JSON data to the plumber type */
} output_t;

/**
 * @brief The thread local used by each worker thread
 **/
typedef struct {
	size_t   size;  /*!< The size of the buffer */
	char*    buf;   /*!< The actual buffer */
} tl_buf_t;

/**
 * @brief Indicates how many times the init function has been called
 **/
static int _init_count;
/**
 * @brief The shared thread locals 
 **/
static pstd_thread_local_t* _tl_bufs;

/**
 * @brief The servlet context 
 **/
typedef struct {
	uint32_t  raw:1;   /*!< Indicates if this servlet takes raw input */
	pipe_t    json;    /*!< The pipe we input JSON string */
	uint32_t  nouts;   /*!< The numer of output ports */
	output_t* outs;    /*!< The output ports */
	pstd_type_model_t*   model;  /*!< The type model */
	pstd_type_accessor_t json_acc;  /*!< The input accessor */
} context_t;

static void* _tl_buf_alloc(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;
	tl_buf_t* ret = (tl_buf_t*)malloc(sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate mmory for the thead local buffer");
	ret->size = 4096;
	if(NULL == (ret->buf = (char*)malloc(ret->size)))
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the buffer memory");
	return ret;
}

static int _tl_buf_dealloc(void* mem, const void* data)
{
	(void)data;
	tl_buf_t* ret = (tl_buf_t*)mem;
	if(ret->buf != NULL) free(ret->buf);
	free(ret);
	return 0;
}

static inline int _tl_buf_resize(tl_buf_t* mem)
{
	char* new_mem = (char*)realloc(mem->buf, mem->size * 2);
	if(NULL == new_mem)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the buffer to size %zu", mem->size * 2);

	mem->size *= 2;
	mem->buf = new_mem;

	return 0;
}

/**
 * @brief Push a new operation to the ops table on the given field
 * @param out    The output buffer
 * @return status code
 **/
static inline int _ensure_space(output_t* out)
{
	if(out->cap < out->nops + 1)
	{
		/* Then we  are going to resize the ops array */
		oper_t* new_arr = (oper_t*)realloc(out->ops, sizeof(oper_t) * out->cap * 2);
		if(NULL == new_arr) ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the operation array");
		out->ops = new_arr;
		out->cap *= 2;
		memset(new_arr + out->nops, 0, sizeof(oper_t) * (out->cap - out->nops));
	}

	return 0;
}

typedef struct {
	output_t*    out;
	const char*  root_type;
	const char*  field_prefix;
	pstd_type_model_t* model;
} _traverse_data_t;

static int _traverse_type(proto_db_field_info_t info, void* data);

static int _process_scalar(proto_db_field_info_t info, const char* actual_name, _traverse_data_t* td)
{
	if(info.primitive_prop == 0 && strcmp(info.type, "plumber/std/request_local/String") != 0)
	{
		size_t prefix_size = strlen(td->field_prefix) + strlen(actual_name) + 2;
		char prefix[prefix_size];
		if(td->field_prefix[0] > 0)
			snprintf(prefix, prefix_size, "%s.%s", td->field_prefix, actual_name);
		/* If this is a complex field */
		_traverse_data_t new_td = {
			.out = td->out,
			.root_type = td->root_type,
			.field_prefix = td->field_prefix[0] ? prefix : actual_name,
			.model = td->model
		};
		if(ERROR_CODE(int) == proto_db_type_traverse(info.type, _traverse_type, &new_td))
			ERROR_RETURN_LOG(int, "Cannot process %s.%s", td->root_type, prefix);
		return 0;
	}

	if(ERROR_CODE(int) == _ensure_space(td->out))
		ERROR_RETURN_LOG(int, "Cannot ensure the output model has enough space");
	oper_t* op = td->out->ops + td->out->nops;
	op->opcode = WRITE;
	op->size = info.size;
	if(strcmp(info.type, "plumber/std/request_local/String") == 0)
		op->type = TYPE_STRING;
	else if(PROTO_DB_FIELD_PROP_REAL & info.primitive_prop)
		op->type = TYPE_FLOAT;
	else if(PROTO_DB_FIELD_PROP_SIGNED & info.primitive_prop)
		op->type = TYPE_SIGNED;
	else 
		op->type = TYPE_UNSIGNED;
	/* TODO: make sure for the string case we only write the token */
	if(ERROR_CODE(pstd_type_accessor_t) == (op->acc = pstd_type_model_get_accessor(td->model, td->out->pipe, actual_name)))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.%s", td->root_type, actual_name);
	td->out->nops ++;
	return 0;
}

static int _build_dimension(proto_db_field_info_t info, _traverse_data_t* td, uint32_t k, const char* actual_name, char* begin, size_t size)
{
	if(k >= info.ndims || (info.ndims - k == 1 && info.dims[k] == 1)) return _process_scalar(info, actual_name, td);

	uint32_t i;
	for(i = 0; i < info.dims[k]; i++)
	{
		size_t rc = (size_t)snprintf(begin, size, "[%u]", i);
		if(ERROR_CODE(int) == _ensure_space(td->out))
			ERROR_RETURN_LOG(int, "Cannont ensure the output model has enough space");
		td->out->ops[td->out->nops].opcode = OPEN_SUBS;
		td->out->ops[td->out->nops].index = i;
		td->out->nops ++;
		if(ERROR_CODE(int) == _build_dimension(info, td, k + 1, actual_name, begin + rc, size - rc))
			ERROR_RETURN_LOG(int, "Cannot build the dimensional data");
		if(ERROR_CODE(int) == _ensure_space(td->out))
			ERROR_RETURN_LOG(int, "Cannont ensure the output model has enough space");
		td->out->ops[td->out->nops].opcode = CLOSE;
		td->out->nops ++;
	}
	return 0;
}

static int _traverse_type(proto_db_field_info_t info, void* data)
{
	_traverse_data_t* td =(_traverse_data_t*)data;

	if(info.is_alias) return 0;
	if(info.size == 0)     return 0;
	if(info.type == NULL) return 0;

	size_t buf_size = strlen(td->field_prefix);
	if(buf_size > 0) buf_size ++;  /* We need add a dot after the prefix if it's nonempty */
	buf_size += strlen(info.name);

	uint32_t i;
	for(i = 0; i< info.ndims; i ++)
	{
		uint32_t d = info.dims[i];
		buf_size += 2;
		for(;d > 0; d /= 10, buf_size ++);
	}

	if(ERROR_CODE(int) == _ensure_space(td->out))
		ERROR_RETURN_LOG(int, "Cannot enough the output model has enough space");
	td->out->ops[td->out->nops].opcode = OPEN;
	if(NULL == (td->out->ops[td->out->nops].field  = strdup(info.name)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dup the field name");
	td->out->nops ++;

	char buf[buf_size + 1];

	if(td->field_prefix[0] == 0)
		snprintf(buf, buf_size + 1, "%s", info.name);
	else
		snprintf(buf, buf_size + 1, "%s.%s", td->field_prefix, info.name);
	

	if(ERROR_CODE(int) == _build_dimension(info, td, 0, buf, buf + strlen(buf), buf_size + 1))
		ERROR_RETURN_LOG(int, "Cannot process the field");

	if(ERROR_CODE(int) == _ensure_space(td->out))
		ERROR_RETURN_LOG(int, "Cannot enough the output model has enough space");
	td->out->ops[td->out->nops].opcode = CLOSE;
	td->out->nops ++;

	return 0;
}


static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	if(argc < 2 || (argc == 2 && 0 == strcmp(argv[1], "--raw")))
		ERROR_RETURN_LOG(int, "Usage: %s [--raw] <name>:<type> [<name>:<type> ...]", argv[0]);
	
	context_t* ctx = (context_t*)ctxbuf;

	if(strcmp(argv[1], "--raw") == 0)
		argc --, argv ++, ctx->raw = 1u;
	else ctx->raw = 0;

	ctx->outs = NULL;
	ctx->model = NULL;

	ctx->nouts = argc - 1;
	if(NULL == (ctx->outs = calloc(ctx->nouts, sizeof(ctx->outs[0]))))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the outputs");

	if(NULL == (ctx->model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create new type model for the servlet");

	if(ERROR_CODE(pipe_t) == (ctx->json = pipe_define("json", PIPE_INPUT, ctx->raw ? "plumber/base/Raw" : "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot define pipe for the JSON input");
	
	if(ERROR_CODE(int) == proto_init())
		ERROR_RETURN_LOG(int, "Cannot intialize libproto");

	uint32_t i;
	for(i = 0; i < ctx->nouts; i ++)
	{
		const char* arg = argv[i + 1];
		char pipe_name[128];
		uint32_t len = 0;
		for(;*arg != 0 && *arg != ':' && len < sizeof(pipe_name) - 1; pipe_name[len++] = *(arg++));
		if(*arg != ':') ERROR_RETURN_LOG(int, "Invalid output descriptor: %s", argv[i + 1]);
		pipe_name[len] = 0;
		const char* type = arg + 1;
		if(ERROR_CODE(pipe_t) == (ctx->outs[i].pipe = pipe_define(pipe_name, PIPE_OUTPUT, type)))
			ERROR_RETURN_LOG(int, "Cannot define the output pipes");

		if(NULL == (ctx->outs[i].name = strdup(pipe_name)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot dup the pipe name");

		if(NULL == (ctx->outs[i].ops = (oper_t*)calloc(ctx->outs[i].cap = 32, sizeof(oper_t))))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the operation array");

		_traverse_data_t td = {
			.model = ctx->model,
			.out   = ctx->outs + i,
			.root_type = type,
			.field_prefix = ""
		};
		if(ERROR_CODE(int) == proto_db_type_traverse(type, _traverse_type, &td))
		{
			const proto_err_t* err = proto_err_stack();
			static char buf[1024];
			for(;err;err = err->child)
				LOG_ERROR("Libproto: %s", proto_err_str(err, buf, sizeof(buf)));
			ERROR_RETURN_LOG(int, "Cannot traverse the type %s", type);
		}

	}

	if(ERROR_CODE(int) == proto_finalize())
		ERROR_RETURN_LOG_ERRNO(int, "Cannot finalize libproto");

	if(ctx->raw)
	{
		if(_tl_bufs == NULL && NULL == (_tl_bufs = pstd_thread_local_new(_tl_buf_alloc, _tl_buf_dealloc, NULL)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot initailize the thread local");
	}
	else
	{
		if(ERROR_CODE(pstd_type_accessor_t) == (ctx->json_acc = pstd_type_model_get_accessor(ctx->model, ctx->json, "token")))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot get the token accessor for the input json");
	}

	_init_count ++;

	return 0;
}

static int _cleanup(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	if(NULL != ctx->outs)
	{
		uint32_t i, j;
		for(i = 0; i < ctx->nouts; i ++)
		{
			if(NULL != ctx->outs[i].name) free(ctx->outs[i].name);
			if(ctx->outs[i].ops != NULL)
			{
				for(j = 0; j < ctx->outs[i].nops; j ++)
					if(NULL != ctx->outs[i].ops[j].field)
						free(ctx->outs[i].ops[j].field);
				free(ctx->outs[i].ops);
			}
		}
		free(ctx->outs);
	}

	if(NULL != ctx->model && ERROR_CODE(int) == pstd_type_model_free(ctx->model))
		ERROR_RETURN_LOG(int, "Cannot dispose the type model");

	if(0 == --_init_count && NULL != _tl_bufs && ERROR_CODE(int) == pstd_thread_local_free(_tl_bufs))
		ERROR_RETURN_LOG(int, "Cannot dispose the thread local buffer");

	return 0;
}

static inline int _exec(void* ctxbuf)
{
	int rc = ERROR_CODE(int);
	context_t* ctx = (context_t*)ctxbuf;

	size_t ti_size = pstd_type_instance_size(ctx->model);
	if(ERROR_CODE(size_t) == ti_size)
		ERROR_RETURN_LOG(int, "Cannot get the size of the type model");
	char ti_buf[ti_size];

	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->model, ti_buf);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create new type instance");

	json_object* root_obj = NULL;

	const char* data = NULL;

	if(ctx->raw)
	{
		/* If this servlet is in the raw mode, then we need to read it from the pipe directly */
		tl_buf_t* tl_buf = pstd_thread_local_get(_tl_bufs);
		if(NULL == tl_buf)
			ERROR_LOG_GOTO(ERR, "Cannot get buffer memory from the thread local");
		size_t len = 0;
		for(;;)
		{
			int rc = pipe_eof(ctx->json);
			if(ERROR_CODE(int) == rc)
				ERROR_LOG_GOTO(ERR, "Cannot check if there's more data in the json pipe");

			if(rc) break;

			size_t bytes_read = pipe_read(ctx->json, tl_buf->buf, tl_buf->size - len);
			if(ERROR_CODE(size_t) == bytes_read)
				ERROR_LOG_GOTO(ERR, "Cannot read data from buffer");

			len += bytes_read;
			if(len + 1 >= tl_buf->size && ERROR_CODE(int) == _tl_buf_resize(tl_buf))
				ERROR_LOG_GOTO(ERR, "Cannot resize the buffer");
		}
		data = tl_buf->buf;
		tl_buf->buf[len] = 0;
	}
	else
	{
		/* If this data comes from the RLS, we need to read the token */
		scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->json_acc);
		if(ERROR_CODE(scope_token_t) == token)
			ERROR_LOG_GOTO(ERR, "Cannot read the token from the json pipe");
		if(NULL == (data = (const char*)pstd_scope_get(token)))
			ERROR_LOG_GOTO(ERR, "Cannot get the string from the given RLS token");
	}

	/* Then we can parse the JSON string */
	if(NULL == (root_obj = json_tokener_parse(data)))
		goto EXIT_NORMALLY;

	uint32_t i;
	for(i = 0; i < ctx->nouts; i ++)
	{
		const output_t* out = ctx->outs + i;
		const char* key = out->name;

		json_object* out_obj;
		
		if(FALSE == json_object_object_get_ex(root_obj, key, &out_obj))
			continue;

		json_object* stack[1024];
		uint32_t sp = 1, pc = 0;
		stack[0] = out_obj;
		for(pc = 0; pc < out->nops; pc ++)
		{
			if(sp == 0) ERROR_LOG_GOTO(ERR, "Invlid stack opeartion");
			json_object* cur_obj = stack[sp - 1];
			const oper_t* op = out->ops + pc;
			switch(op->opcode)
			{
				case OPEN:
					if(sp >= sizeof(stack)) ERROR_LOG_GOTO(ERR, "Operation stack overflow");
					if(cur_obj != NULL)
					{
						if(FALSE == json_object_object_get_ex(cur_obj, op->field, stack + sp))
						{
							stack[sp] = NULL;
							LOG_NOTICE("Missing field %s", op->field);
						}
					}
					else stack[sp] = NULL;
					sp ++;
					break;
				case OPEN_SUBS:
					if(sp >= sizeof(stack)) ERROR_LOG_GOTO(ERR, "Operation stack overflow");
					if(cur_obj != NULL)
					{
						if(NULL == (stack[sp] = json_object_array_get_idx(cur_obj, (int)op->index)))
							LOG_NOTICE("Missing subscript %u", op->index);
					}
					else stack[sp] = NULL;
					sp ++; 
					break;
				case CLOSE:
					if(NULL != cur_obj) json_object_put(cur_obj);
					sp --;
					break;
				case WRITE:
					switch(op->type)
					{
						case TYPE_SIGNED:
						case TYPE_UNSIGNED:
						{
							int64_t value = json_object_get_int(cur_obj);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#	error("This doesn't work with big endian archtechture")
#endif
							/* In this case we must expand the sign bit */
							if(op->type == TYPE_SIGNED && value < 0)
								value |= (-1ll << (8 * op->size - 1));
							
							if(ERROR_CODE(int) == pstd_type_instance_write(inst, op->acc, &value, op->size))
								ERROR_LOG_GOTO(ERR, "Cannot write field");
							break;
						}
						case TYPE_FLOAT:
						{
							double d_value = json_object_get_double(cur_obj);
							float  f_value = (float)d_value;
							void* data = op->size == sizeof(double) ? (void*)&d_value : (void*)&f_value;
							if(ERROR_CODE(int) == pstd_type_instance_write(inst, op->acc, &data, op->size))
								ERROR_LOG_GOTO(ERR, "Cannot write field");
							break;
						}
						case TYPE_STRING:
						{
							const char* str = json_object_get_string(cur_obj);
							if(NULL == str) ERROR_LOG_GOTO(ERR, "Cannot get the string value");
							size_t len = strlen(str);
							pstd_string_t* pstd_str = pstd_string_new(len + 1);
							if(NULL == pstd_str) ERROR_LOG_GOTO(ERR, "Cannot allocate new pstd string object");
							if(ERROR_CODE(size_t) == pstd_string_write(pstd_str, str, len))
							{
								pstd_string_free(pstd_str);
								ERROR_LOG_GOTO(ERR, "Cannot write string to the pstd string object");
							}

							scope_token_t token = pstd_string_commit(pstd_str);
							if(ERROR_CODE(scope_token_t) == token)
							{
								pstd_string_free(pstd_str);
								ERROR_LOG_GOTO(ERR, "Cannot commit the string to the RLS");
							}
							/* From this point, we lose the ownership of the RLS object */
							if(ERROR_CODE(int) == pstd_type_instance_write(inst, op->acc, &data, sizeof(scope_token_t)))
								ERROR_LOG_GOTO(ERR, "Cannot write the RLS token to the output pipe");
							break;
						}
					}
			}
		}
	}

EXIT_NORMALLY:
	rc = 0;
ERR:
	if(NULL != root_obj) json_object_put(root_obj);
	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");
	return rc;
}

SERVLET_DEF = {
	.desc = "Parse the JSON to the given type",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _cleanup,
	.exec   = _exec
};
