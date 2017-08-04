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
	OPEN,    /*!< This means we should open an object for write */
	CLOSE,   /*!< This means we should close the object because nothing to write */
	WRITE    /*!< We need to write the primitive to the type */
} opcode_t;

/**
 * @brief The object mapping operations
 **/
typedef struct {
	opcode_t    opcode;        /*!< The operation code */
	char*       field;         /*!< Only used for opening a field: The field name we need to open */
	pstd_type_accessor_t acc;  /*!< Only used for primitive field: The accessor we should use */
	size_t               size; /*!< Only used for primitive: The size of the data field */
	enum {
		TYPE_SIGNED,              /*!< This is an integer */
		TYPE_UNSIGNED,            /*!< This is a unsigned integer */
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
	}

	return 0;
}

/**
 * @brief resolve the types and fill the operation array
 * @param type The type name to resolve
 * @param out The output context
 * @return status code
 **/
static int _resolve_type(const char* base_type, const char* field_expr, output_t* out)
{
	/* Step 1 : We need get the actual type of the part we want to process */
	const char* type = base_type;
	if(field_expr[0] != 0 && NULL == (type = proto_db_field_type(base_type, field_expr)))
		ERROR_RETURN_LOG(int, "Cannot get the field type of %s.%s", base_type, field_expr);

	/* Step 2 : Get the protocol object from the protodb */
	const proto_type_t* proto = proto_db_query_type(type);
	if(NULL == proto)
		ERROR_RETURN_LOG(int, "Cannot get the type information for type %s", type);

	uint32_t nent = proto_type_get_size(proto);
	if(ERROR_CODE(uint32_t) == nent)
		ERROR_RETURN_LOG(int, "Cannot get the size of the protocol definition");

	char pwd[PATH_MAX];
	size_t pwd_len = strlen(type);
	if(pwd_len >= sizeof(pwd)) ERROR_RETURN_LOG(int, "Type name too long");
	memcpy(pwd, type, pwd_len + 1);
	for(;pwd_len > 0 && pwd[pwd_len - 1] != '/'; pwd[--pwd_len] = 0);

	/* Then we need to go through this type to figure out what is needed */
	const proto_type_entity_t* base = proto_type_get_entity(proto, 0);
	uint32_t ent_begin = 0;
	if(NULL != base && base->symbol == NULL && base->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
	{
		/* This is the base type of the type, we resolve it recursively */
		const char* rel_type = proto_ref_typeref_get_path(base->type_ref);
		if(NULL == rel_type) ERROR_RETURN_LOG(int, "Cannot get the type name of the base type");
		const char* full_type = proto_cache_full_name(rel_type, pwd);
		if(NULL == full_type) ERROR_RETURN_LOG(int, "Cannot get the full name of the base type");
		if(ERROR_CODE(int) == _resolve_type(full_type, field_expr, out))
			ERROR_RETURN_LOG(int, "Cannot resolve the base type %s", full_type);
		/* Because we already resolved the base type, so we start from 1 */
		ent_begin = 1; 
	}

	uint32_t i;
	for(i = ent_begin; i < nent; i ++)
	{
		const proto_type_entity_t* ent = proto_type_get_entity(proto, i);
		if(NULL == ent) ERROR_RETURN_LOG(int, "Cannot get the entiy at offest %u", i);
		if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
		{
			if(NULL == ent->symbol)
				ERROR_RETURN_LOG(int, "Bug: duplicated base type entity");
			else
			{
				/* In here we need to open it */
				const char* rel_type = proto_ref_typeref_get_path(ent->type_ref);
				if(NULL == rel_type)
					ERROR_RETURN_LOG(int, "Cannot get the relative type path to the field type");
				const char* full_type = proto_cache_full_name(rel_type, pwd);
				if(NULL == full_type)
					ERROR_RETURN_LOG(int, "Cannot get the full type name of the field type");

				if(strcmp(full_type, "plumber/std/request_local/String") == 0)
				{
					/* TODO: This is a string primitive in JSON actually */
				}
				else 
				{
					/* TODO: This is a complex type */

				}
			}
		}
		else if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_NONE)
		{
			/* TODO: handle the primitive */
		}
		/* And we do not really care about the alias */
	}

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

		if(ERROR_CODE(int) == _resolve_type(type, "", ctx->outs + i))
			ERROR_RETURN_LOG(int, "Cannot resolve the type %s", type);
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
			for(j = 0; j < ctx->outs[i].nops; j ++)
				if(NULL != ctx->outs[i].ops[j].field) free(ctx->outs[i].ops[j].field);
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
