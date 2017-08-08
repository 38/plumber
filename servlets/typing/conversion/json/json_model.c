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

#include <json_model.h>


/**
 * @brief The internal data structure we used to traverse the type
 **/
typedef struct {
	json_model_t*       json_model;               /*!< The type model we are exmaming */
	const char*         root_type;           /*!< The type of the pipe */
	const char*         field_prefix;        /*!< The prefix to the filed are are examing */
	pstd_type_model_t*  type_model;    /*!< The PSTD type model */
} _traverse_data_t;

static inline void _print_libproto_err()
{
	const proto_err_t* err = proto_err_stack();
	static char buf[1024];
	for(;err;err = err->child)
		LOG_ERROR("Libproto: %s", proto_err_str(err, buf, sizeof(buf)));
}

static int _traverse_type(proto_db_field_info_t info, void* data);

/**
 * @brief Push a new operation to the ops table on the given field
 * @param out    The output buffer
 * @return status code
 **/
static inline int _ensure_space(json_model_t* jm)
{
	if(jm->cap < jm->nops + 1)
	{
		/* Then we  are going to resize the ops array */
		json_model_op_t* new_arr = (json_model_op_t*)realloc(jm->ops, sizeof(json_model_op_t) * jm->cap * 2);
		if(NULL == new_arr) ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the operation array");
		jm->ops = new_arr;
		jm->cap *= 2;
		memset(new_arr + jm->nops, 0, sizeof(json_model_op_t) * (jm->cap - jm->nops));
	}
	return 0;
}

/**
 * @brief process a scalar type 
 * @param info The field info for this type
 * @param actual_name The actual field expression
 * @param td The traverse context
 * @return status code
 **/
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
			.json_model   = td->json_model,
			.root_type    = td->root_type,
			.field_prefix = td->field_prefix[0] ? prefix : actual_name,
			.type_model   = td->type_model
		};
		if(ERROR_CODE(int) == proto_db_type_traverse(info.type, _traverse_type, &new_td))
		{
			_print_libproto_err();
			ERROR_RETURN_LOG(int, "Cannot process %s.%s", td->root_type, prefix);
		}
		return 0;
	}

	if(ERROR_CODE(int) == _ensure_space(td->json_model))
		ERROR_RETURN_LOG(int, "Cannot ensure the output model has enough space");
	json_model_op_t* op = td->json_model->ops + td->json_model->nops;
	op->opcode = JSON_MODEL_OPCODE_WRITE;
	op->size = info.size;
	if(strcmp(info.type, "plumber/std/request_local/String") == 0)
		op->type = JSON_MODEL_TYPE_STRING;
	else if(PROTO_DB_FIELD_PROP_REAL & info.primitive_prop)
		op->type = JSON_MODEL_TYPE_FLOAT;
	else if(PROTO_DB_FIELD_PROP_SIGNED & info.primitive_prop)
		op->type = JSON_MODEL_TYPE_SIGNED;
	else 
		op->type = JSON_MODEL_TYPE_UNSIGNED;
	/* TODO: make sure for the string case we only write the token */
	if(ERROR_CODE(pstd_type_accessor_t) == (op->acc = pstd_type_model_get_accessor(td->type_model, td->json_model->pipe, actual_name)))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.%s", td->root_type, actual_name);
	td->json_model->nops ++;
	return 0;
}

static int _build_dimension(proto_db_field_info_t info, _traverse_data_t* td, uint32_t k, const char* actual_name, char* begin, size_t size)
{
	if(k >= info.ndims || (info.ndims - k == 1 && info.dims[k] == 1)) return _process_scalar(info, actual_name, td);

	uint32_t i;
	for(i = 0; i < info.dims[k]; i++)
	{
		size_t rc = (size_t)snprintf(begin, size, "[%u]", i);
		if(ERROR_CODE(int) == _ensure_space(td->json_model))
			ERROR_RETURN_LOG(int, "Cannont ensure the output model has enough space");
		td->json_model->ops[td->json_model->nops].opcode = JSON_MODEL_OPCODE_OPEN_SUBS;
		td->json_model->ops[td->json_model->nops].index = i;
		td->json_model->nops ++;
		if(ERROR_CODE(int) == _build_dimension(info, td, k + 1, actual_name, begin + rc, size - rc))
			ERROR_RETURN_LOG(int, "Cannot build the dimensional data");
		if(ERROR_CODE(int) == _ensure_space(td->json_model))
			ERROR_RETURN_LOG(int, "Cannont ensure the output model has enough space");
		td->json_model->ops[td->json_model->nops].opcode = JSON_MODEL_OPCODE_CLOSE;
		td->json_model->nops ++;
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

	if(ERROR_CODE(int) == _ensure_space(td->json_model))
		ERROR_RETURN_LOG(int, "Cannot enough the output model has enough space");
	td->json_model->ops[td->json_model->nops].opcode = JSON_MODEL_OPCODE_OPEN;
	if(NULL == (td->json_model->ops[td->json_model->nops].field  = strdup(info.name)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dup the field name");
	td->json_model->nops ++;

	char buf[buf_size + 1];

	if(td->field_prefix[0] == 0)
		snprintf(buf, buf_size + 1, "%s", info.name);
	else
		snprintf(buf, buf_size + 1, "%s.%s", td->field_prefix, info.name);
	

	if(ERROR_CODE(int) == _build_dimension(info, td, 0, buf, buf + strlen(buf), buf_size + 1))
		ERROR_RETURN_LOG(int, "Cannot process the field");

	if(ERROR_CODE(int) == _ensure_space(td->json_model))
		ERROR_RETURN_LOG(int, "Cannot enough the output model has enough space");
	td->json_model->ops[td->json_model->nops].opcode = JSON_MODEL_OPCODE_CLOSE;
	td->json_model->nops ++;

	return 0;
}

json_model_t* json_model_new(const char* pipe_name, const char* type_name, int input, pstd_type_model_t* type_model, void* mem)
{
	if(NULL == pipe_name || NULL == type_name || NULL == mem)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	json_model_t* ret = (json_model_t*)mem;
	memset(ret, 0, sizeof(json_model_t));

	if(ERROR_CODE(pipe_t) == (ret->pipe = pipe_define(pipe_name, input ? PIPE_INPUT : PIPE_OUTPUT, type_name)))
		ERROR_PTR_RETURN_LOG("Cannot define the output pipes");

	if(NULL == (ret->name = strdup(pipe_name)))
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot dup the pipe name");

	if(NULL == (ret->ops = (json_model_op_t*)calloc(ret->cap = 32, sizeof(json_model_op_t))))
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the operation array");

	proto_db_field_info_t info;
	int adhoc_rc = proto_db_is_adhoc(type_name, &info);
	if(ERROR_CODE(int) == adhoc_rc)
		ERROR_PTR_RETURN_LOG("Cannot check if the type is an adhoc type");

	int is_str = 0;

	if(adhoc_rc || (is_str = (strcmp(type_name, "plumber/std/request_local/String") == 0)))
	{
		ret->ops[0].opcode = JSON_MODEL_OPCODE_WRITE;
		ret->ops[0].size   = info.size;
		
		if(ERROR_CODE(pstd_type_accessor_t) == (ret->ops[0].acc = pstd_type_model_get_accessor(type_model, ret->pipe, is_str ? "token" : "value")))
			ERROR_PTR_RETURN_LOG("Cannot get the accessor for primitive type %s", type_name);
		ret->nops = 1;
		return 0;
	}

	_traverse_data_t td = {
		.type_model   = type_model,
		.json_model   = ret,
		.root_type    = type_name,
		.field_prefix = ""
	};

	if(ERROR_CODE(int) == proto_db_type_traverse(type_name, _traverse_type, &td))
	{
		_print_libproto_err();
		ERROR_PTR_RETURN_LOG("Cannot traverse the type %s", type_name);
	}

	return ret;
}


int json_model_free(json_model_t* model)
{
	if(NULL == model)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t j;

	if(NULL != model->name) free(model->name);
	if(model->ops != NULL)
	{
		for(j = 0; j < model->nops; j ++)
			if(NULL != model->ops[j].field)
				free(model->ops[j].field);
		free(model->ops);
	}

	return 0;
}
