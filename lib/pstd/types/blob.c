/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <error.h>

#include <pservlet.h>

#include <proto.h>
#include <pstd/type.h>
#include <pstd/types/blob.h>

/**
 * @brief The actual data structure for a blob model
 **/
struct _pstd_blob_model_t {
	char*         type_name;   /*!< The typename of this blob */
	uint32_t      blob_size;   /*!< The actual size of the blob */
	uint32_t      num_tokens;  /*!< The number of tokens in this type */
	uint32_t      cap_tokens;  /*!< The capacity of the token array */
	uint32_t*     token_offset;/*!< The array used to map the token field expression to ID */
};

/**
 * @brief The data structure for traversing the type
 **/
typedef struct {
	pstd_blob_model_t*  target;       /*!< The target blob model */
	const char*         field_prefix; /*!< The field prefix use used to access current field */
	const char*         root_type;    /*!< The root type of the field */
} _traverse_data_t;

static int _traverse_type(proto_db_field_info_t info, void* data);

static int _process_scalar(proto_db_field_info_t info, const char* actual_name, _traverse_data_t* td)
{
	if(info.primitive_prop == 0)
	{
		/* If this isn't a primitive type */
		
		char* prefix = NULL;
		size_t prefix_size = strlen(td->field_prefix) + strlen(actual_name) + 2;

		/* Since the prefix_size can't be 0, this is safe. But if prefix_size is 0, ((prefix_size - 1) & 0xff + 1) = 256 */
		if(prefix_size <= 256)
		    prefix = alloca(((prefix_size - 1) & 0xff) + 1);
		else
		    prefix = malloc(prefix_size);

		if(NULL == prefix)
		    ERROR_RETURN_LOG(int, "Cannot allocate memory for the prefix %s.%s", td->field_prefix, actual_name);

		if(td->field_prefix[0] != 0)
			snprintf(prefix, prefix_size, "%s.%s", td->field_prefix, actual_name);
		
		_traverse_data_t new_td = {
			.target       = td->target,
			.field_prefix = td->field_prefix[0] ? prefix : actual_name,
			.root_type    = td->root_type
		};

		if(ERROR_CODE(int) == proto_db_type_traverse(info.type, _traverse_type, &new_td))
			ERROR_LOG_GOTO(COMPOUND_ERR, "Cannot traverse the inner field type %s", td->field_prefix[0] ? prefix : actual_name);

		if(prefix_size > 256) free(prefix);

		return 0;

COMPOUND_ERR:
		if(prefix_size > 256 && NULL != prefix) free(prefix);
		return ERROR_CODE(int);
	}

	if(info.primitive_prop & PROTO_DB_FIELD_PROP_SCOPE)
	{
		LOG_TRACE("Found scope token %s in type %s", actual_name, td->root_type);

		if(td->target->cap_tokens <= td->target->num_tokens)
		{
			uint32_t* new_array = (uint32_t*)realloc(td->target->token_offset, sizeof(uint32_t) * td->target->cap_tokens * 2);

			if(new_array == NULL)
				ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the token offset array");

			td->target->token_offset = new_array;
			td->target->cap_tokens *= 2;
		}
		
		uint32_t size = 0;
		uint32_t offset = proto_db_type_offset(td->root_type, actual_name, &size);

		if(ERROR_CODE(uint32_t) == offset)
			ERROR_RETURN_LOG(int, "Cannot query the offset of field %s.%s", td->root_type, actual_name);

		if(size != sizeof(scope_token_t))
			ERROR_RETURN_LOG(int, "Unexpected token size for field %s.%s", td->root_type, actual_name);

		td->target->token_offset[td->target->num_tokens ++] = offset;
		
		return 0;
	}

	return 0;
}

static int _build_dimension(proto_db_field_info_t info, _traverse_data_t* td, uint32_t k, const char* actual_name, char* begin, size_t size)
{
	if(k >= info.ndims || (info.ndims - k == 1 && info.dims[k] == 1)) return _process_scalar(info, actual_name, td);

	uint32_t i;
	for(i = 0; i < info.dims[k]; i++)
	{
		size_t rc = (size_t)snprintf(begin, size, "[%u]", i);
		if(ERROR_CODE(int) == _build_dimension(info, td, k + 1, actual_name, begin + rc, size - rc))
		    ERROR_RETURN_LOG(int, "Cannot build the dimensional data");
	}
	return 0;
}

static int _traverse_type(proto_db_field_info_t info, void* data)
{
	_traverse_data_t* td = (_traverse_data_t*)data;

	if(info.is_alias || info.size == 0)
		return 0;
	
	/* First we need to figureout the size of the buffer we want to use */
	size_t buf_size = strlen(td->field_prefix);
	if(buf_size > 0) buf_size ++;
	buf_size += strlen(info.name);

	uint32_t i;
	for(i = 0; i< info.ndims; i ++)
	{
		uint32_t d = info.dims[i];
		buf_size += 2;
		for(;d > 0; d /= 10, buf_size ++);
	}

	/* Then we need to allocate the buffer for this */
	char* buf = buf_size < 256 ? (char*)alloca((buf_size&0xff) + 1) : (char*)malloc(buf_size + 1);
	if(NULL == buf) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the name buffer");

	if(td->field_prefix[0] == 0)
	    snprintf(buf, buf_size + 1, "%s", info.name);
	else
	    snprintf(buf, buf_size + 1, "%s.%s", td->field_prefix, info.name);

	if(ERROR_CODE(int) == _build_dimension(info, td, 0, buf, buf + strlen(buf), buf_size + 1))
	    ERROR_LOG_GOTO(ERR, "Cannot process the field");
	
	if(buf_size >= 256) free(buf);
	return 0;
ERR:
	if(buf_size >= 256 && NULL != buf) free(buf);
	return ERROR_CODE(int);
}

static int _offset_compare(const void* a, const void* b)
{
	uint32_t va = *(const uint32_t*)a;
	uint32_t vb = *(const uint32_t*)b;

	if(va > vb) return 1;
	if(vb > va) return -1;
	return 0;
}

pstd_blob_model_t* pstd_blob_model_new(const char* type_name)
{
	if(NULL == type_name)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(ERROR_CODE(int) == proto_db_init())
		ERROR_PTR_RETURN_LOG("Cannot initialize the protocol database");

	pstd_blob_model_t* ret = (pstd_blob_model_t*)calloc(sizeof(pstd_blob_model_t), 1);

	if(NULL == ret)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the blob model");

	if(NULL == (ret->type_name = strdup(type_name)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the typename");

	if(ERROR_CODE(uint32_t) == (ret->blob_size = proto_db_type_size(type_name)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot query the size of the type: %s", type_name);

	ret->num_tokens = 0;
	ret->cap_tokens = 32;

	if(NULL == (ret->token_offset = (uint32_t*)malloc(sizeof(uint32_t) * ret->cap_tokens)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the token array");

	_traverse_data_t td = {
		.target       = ret,
		.field_prefix = "",
		.root_type    = type_name
	};

	if(ERROR_CODE(int) == proto_db_type_traverse(type_name, _traverse_type, &td))
		ERROR_LOG_GOTO(ERR, "Cannot traverse the type: %s", type_name);

	if(ERROR_CODE(int) == proto_db_finalize())
		LOG_WARNING("Cannot finalize libproto");
	
	qsort(ret->token_offset, ret->num_tokens, sizeof(uint32_t), _offset_compare);

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->token_offset)
			free(ret->token_offset);
		if(NULL != ret->type_name)
			free(ret->type_name);
		free(ret);
	}
	proto_db_finalize();

	return NULL;
}

uint32_t pstd_blob_model_num_tokens(pstd_blob_model_t* model)
{
	if(NULL == model) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	return model->num_tokens;
}

int pstd_blob_model_free(pstd_blob_model_t* model)
{
	if(NULL == model)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != model->token_offset)
		free(model->token_offset);

	if(NULL != model->type_name)
		free(model->type_name);

	free(model);

	return 0;
}

pstd_blob_token_idx_t pstd_blob_model_token_idx(const pstd_blob_model_t* model, const char* field_expr)
{
	pstd_blob_token_idx_t ret = ERROR_CODE(pstd_blob_token_idx_t);

	if(NULL == model || NULL == field_expr)
		ERROR_RETURN_LOG(pstd_blob_token_idx_t, "Invalid arguments");
	
	if(ERROR_CODE(int) == proto_db_init())
		ERROR_LOG_GOTO(RET, "Cannot initialize the protocol data");

	uint32_t offset;
	uint32_t size;
	proto_db_field_prop_t prop;

	if(ERROR_CODE(proto_db_field_prop_t) == (prop = proto_db_field_type_info(model->type_name, field_expr)))
		ERROR_LOG_GOTO(RET, "Cannot query the field property for field %s in type %s", field_expr, model->type_name);

	if(!(prop & PROTO_DB_FIELD_PROP_SCOPE))
		ERROR_LOG_GOTO(RET, "Invalid field accessor: not a token accessor field:, %s type: %s", field_expr, model->type_name);

	if(ERROR_CODE(uint32_t) == (offset = proto_db_type_offset(model->type_name, field_expr, &size)))
		ERROR_LOG_GOTO(RET, "Cannot query the offset the field %s of type %s", field_expr, model->type_name);

	if(size != sizeof(scope_token_t))
		ERROR_LOG_GOTO(RET, "Unexpected token token size");

	/* Then we need to figure out which one is the field we are looking at */
	uint32_t left = 0, right = model->num_tokens;

	if(model->num_tokens == 0 || offset <  model->token_offset[0] || model->token_offset[model->num_tokens - 1] < offset)
		ERROR_LOG_GOTO(RET, "Cannot find the token offset %u in the blob type model for type %s. (Field expression: %s)", offset, model->type_name, field_expr);

	while(right - left > 1)
	{
		uint32_t mid = (left + right) / 2;

		if(model->token_offset[mid] <= offset) left = mid;
		else right = mid;
	}

	ret = left;
RET:
	if(ERROR_CODE(int) == proto_db_finalize())
		ERROR_RETURN_LOG(pstd_blob_token_idx_t, "Cannot finalize the protocol database");

	return ret;
}
