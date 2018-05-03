/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>
#include <psnl.h>

#include <pstd/types/string.h>

#include <options.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	pipe_t             in;           /*!< The input text */ 
	pipe_t             out;          /*!< The output field */
	pstd_type_model_t* type_model;   /*!< The type model for current servlet */
	pstd_type_accessor_t  a_in_tok;    /*!< The input token */
	pstd_type_accessor_t  a_out_tok;   /*!< The output token */
	options_t          options;      /*!< The servlet option */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	ctx->type_model = NULL;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the options");

	PIPE_LIST(pipes)
	{
		PIPE("input",     PIPE_INPUT,           ctx->options.input_type,          ctx->in),
		PIPE("output",    PIPE_OUTPUT,          ctx->options.result_type,         ctx->out)
	};
	
	if(ERROR_CODE(int) == PIPE_BATCH_INIT(pipes)) return ERROR_CODE(int);

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the type model");

	if(!ctx->options.raw && ERROR_CODE(pstd_type_accessor_t) == (ctx->a_in_tok = pstd_type_model_get_accessor(ctx->type_model, ctx->in, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the input token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_out_tok = pstd_type_model_get_accessor(ctx->type_model, ctx->out, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the output token");

	return 0;
}

static int _cleanup(void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	int rc = 0;

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == options_free(&ctx->options))
		rc = ERROR_CODE(int);

	return rc;
}

SERVLET_DEF = {
	.desc = "The parser to parse a initial field configuration",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _cleanup
};
