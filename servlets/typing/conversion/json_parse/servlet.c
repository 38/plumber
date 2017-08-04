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
	opcode_t    opcode;   /*!< The operation code */
	char*       field;    /*!< The field name we need to open */
	pstd_type_accessor_t acc;  /*!< The accessor we should use */
	size_t      size;     /*!< The size we need to write */
} oper_t;

/**
 * @brief The output spec for each output ports
 **/
typedef struct {
	pipe_t         pipe;       /*!< The pipe we want to produce the contents */
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

static int _type_determined(pipe_t pipe, const char* type, const void* data)
{
	(void)pipe;
	(void)type;
	(void)data;
	int rc = 0;
	if(ERROR_CODE(int) == proto_init())
		ERROR_RETURN_LOG(int, "Cannot intialize libproto");

	goto EXIT;
EXIT:
	if(ERROR_CODE(int) == proto_finalize())
		ERROR_RETURN_LOG(int, "Cannot finalize libproto");

	return rc;
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

		if(ERROR_CODE(int) == pstd_type_model_assert(ctx->model, ctx->outs[i].pipe, _type_determined, ctx->outs + i))
			ERROR_RETURN_LOG(int, "Cannot define install the type determined callback function for pipe %s", pipe_name);
	}

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
