/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <json.h>

#include <pstd.h>
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
	uint32_t       cap;       /*!< The capacity of the operation array */
	uint32_t       nops;       /*!< The number of operations we need to be done for this type */
	oper_t*        ops;        /*!< The operations we need to dump the JSON data to the plumber type */
} output_t;

/**
 * @brief The servlet context 
 **/
typedef struct {
	uint32_t  raw:1;   /*!< Indicates if this servlet takes raw input */
	pipe_t    json;    /*!< The pipe we input JSON string */
	uint32_t  nouts;   /*!< The numer of output ports */
	output_t* outs;    /*!< The output ports */
	pstd_type_model_t* model;  /*!< The type model */
} context_t;

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
	if(info.primitive_prop == 0 && strcmp(info.type, "plumber/std/request_local/String") == 0)
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

	return 0;
}

SERVLET_DEF = {
	.desc = "Parse the JSON to the given type",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _cleanup
};
