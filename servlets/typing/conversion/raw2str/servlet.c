/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>

#include <pservlet.h>
#include <pstd.h>

#include <pstd/types/string.h>

typedef struct {
	pipe_t               input;       /*!< The input pipe */
	pipe_t               output;      /*!< The output pipe */
	pstd_type_model_t*   type_model;  /*!< The type model */
	pstd_type_accessor_t output_token; /*!< The input token */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	(void)argc;
	(void)argv;
	context_t* ctx = (context_t*)ctxbuf;

	if(ERROR_CODE(pipe_t) == (ctx->input = pipe_define("input", PIPE_INPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the input pipe");

	if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("output", PIPE_OUTPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create type model");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->output_token = pstd_type_model_get_accessor(ctx->type_model, ctx->output, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for plumber/std/request_local/String.token");

	return 0;
}

static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	int rc = ERROR_CODE(int);

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create the instance");

	pstd_string_t* string = pstd_string_new(32);

	for(;;)
	{
		int eof_rc = pipe_eof(ctx->input);
		if(ERROR_CODE(int) == eof_rc)
			ERROR_LOG_GOTO(ERR, "Cannot check if the input comes to end");
		else if(eof_rc) break;

		char readbuf[1024];
		size_t read_rc = pipe_read(ctx->input, readbuf, sizeof(readbuf));
		if(ERROR_CODE(size_t) == read_rc)
			ERROR_LOG_GOTO(ERR, "Cannot read from the input");

		if(ERROR_CODE(size_t) == pstd_string_write(string, readbuf, read_rc))
			ERROR_LOG_GOTO(ERR, "Cannot write to string");
	}

	scope_token_t token = pstd_string_commit(string);
	if(ERROR_CODE(scope_token_t)  == token)
		ERROR_LOG_GOTO(ERR, "Cannot commit to RLS");
	else
		string = NULL;

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->output_token, token))
		ERROR_LOG_GOTO(ERR, "Cannot write token to output");

	rc = 0;
ERR:
	if(NULL != string && ERROR_CODE(int) == pstd_string_free(string))
		rc = ERROR_CODE(int);

	if(NULL != inst && ERROR_CODE(int) == pstd_type_instance_free(inst))
		rc = ERROR_CODE(int);

	return rc;
}

static int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	return pstd_type_model_free(ctx->type_model);
}

SERVLET_DEF = {
	.desc = "Convert a Raw to String",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
