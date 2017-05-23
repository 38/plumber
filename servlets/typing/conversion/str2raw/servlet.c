/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>

#include <pstd/types/string.h>

typedef struct {
	pipe_t               input;       /*!< The input pipe */
	pipe_t               output;      /*!< The output pipe */
	pstd_type_model_t*   type_model;  /*!< The type model */
	pstd_type_accessor_t input_token; /*!< The input token */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	(void)argc;
	(void)argv;
	context_t* ctx = (context_t*)ctxbuf;

	if(ERROR_CODE(pipe_t) == (ctx->input = pipe_define("input", PIPE_INPUT, "plumber/std/request_local/String")))
	    ERROR_RETURN_LOG(int, "Cannot define the input pipe");

	if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("output", PIPE_OUTPUT, NULL)))
	    ERROR_RETURN_LOG(int, "Cannot define the output pipe");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
	    ERROR_RETURN_LOG(int, "Cannot create type model");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->input_token = pstd_type_model_get_accessor(ctx->type_model, ctx->input, "token")))
	    ERROR_RETURN_LOG(int, "Cannot get the accessor for plumber/std/request_local/String.token");

	return 0;
}

static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	size_t sz = pstd_type_instance_size(ctx->type_model);
	if(ERROR_CODE(size_t) == sz)
	    ERROR_RETURN_LOG(int, "Cannot get the size of the model");

	char buf[sz];
	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->type_model, buf);
	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Cannot create the instance");

	pstd_bio_t* out = pstd_bio_new(ctx->output);
	if(NULL == out) ERROR_LOG_GOTO(ERR, "Cannot create the BIO object");

	scope_token_t token;
	const pstd_string_t* input_str;
	const char* input_c_str;
	int rc = 0;

	if(ERROR_CODE(scope_token_t) == (token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->input_token)))
	    ERROR_LOG_GOTO(ERR, "Cannot read primitive from the type header");

	if(NULL == (input_str = pstd_string_from_rls(token)))
	    ERROR_LOG_GOTO(ERR, "Cannot get string object by the RLS token");

	if(NULL == (input_c_str = pstd_string_value(input_str)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the string value from the RLS string object");

	if(ERROR_CODE(size_t) == pstd_bio_puts(out, input_c_str))
	    ERROR_LOG_GOTO(ERR, "Cannot write the string value to output");

	rc = 0;
ERR:
	if(NULL != out && ERROR_CODE(int) == pstd_bio_free(out))
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
	.desc = "Convert a string to Raw",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
