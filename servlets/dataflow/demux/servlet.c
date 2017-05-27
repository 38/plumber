/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

typedef struct {
	enum {
		MODE_MATCH,         /*!< The mode that inputs a string as condition and do exact matching */
		MODE_REGEX,         /*!< The mode that inputs a string as condition and do regular expression match */
		MODE_NUMERIC        /*!< The mode that inputs a number from 0 to N */
	}            mode;      /*!< The mode of the dexmuer */
	const char*  field;     /*!< The field expression we want to read */
	uint32_t     regex:1;   /*!< If this demuxer uses regular expression as pattern */
	uint32_t     numeric:1; /*!< If this demuxer uses number (0 to N) as pattern */

	uint32_t     ncond;     /*!< The number of condition we want to match */
	pipe_t       cond;      /*!< The pipe that inputs the condition input */
	pipe_t       data;      /*!< The pipe that contains the data */
	pipe_t*      output;    /*!< The pipes for the outputs */

	pstd_type_model_t*    type_model;      /*!< Type model */
	pstd_type_accessor_t  cond_acc;        /*!< The condition accessor */
} context_t;
static int _set_option(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* args)
{
	(void)nparams;
	(void)n;
	char what = options[idx].short_opt;
	context_t* ctx = (context_t*)args;
	int expected_mode;
	const char* field;
	switch(what)
	{
		case 'r':
			expected_mode = MODE_REGEX;
			field = "token";
			goto SET_MODE;
		case 'n':
			if(nparams != 1 || params[0].type != PSTD_OPTION_STRING)
				ERROR_RETURN_LOG(int, "The field expression is expected");
			expected_mode = MODE_NUMERIC;
			field = params[0].strval; 
SET_MODE:
			if(ctx->mode != MODE_MATCH)
				ERROR_RETURN_LOG(int, "Only one mode specifier can be passed");
			ctx->mode = expected_mode;
			ctx->field = field;
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid option");
	}

	return 0;
}
static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	static pstd_option_t opts[] = {
		{
			.long_opt    = "regex",
			.short_opt   = 'r',
			.description = "Use the regular expression mode",
			.pattern     = "",
			.handler     = _set_option
		},
		{
			.long_opt    = "numeric",
			.short_opt   = 'n',
			.description = "Use the numeric mode",
			.pattern     = "S",
			.handler     = _set_option
		},
		{
			.long_opt    = "help",
			.short_opt   = 'h',
			.description = "Display this help message",
			.pattern     = "",
			.handler     = pstd_option_handler_print_help
		}
	};

	ctx->mode = MODE_MATCH;
	ctx->field = "token";
	ctx->output = NULL;
	ctx->type_model = NULL;
	
	uint32_t opt_rc = pstd_option_parse(opts, sizeof(opts) / sizeof(opts[0]), argc, argv, ctx);

	ctx->ncond = argc - opt_rc;
		
	if(ctx->mode == MODE_NUMERIC)
	{
		if(ERROR_CODE(pipe_t) == (ctx->cond = pipe_define("cond", PIPE_INPUT, "$Tcond")))
			ERROR_RETURN_LOG(int, "Cannot define the condition pipe");
	}
	else if(ERROR_CODE(pipe_t) == (ctx->cond = pipe_define("cond", PIPE_INPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot define the condition pipe");

	if(ERROR_CODE(pipe_t) == (ctx->data = pipe_define("data", PIPE_INPUT, "$Tdata")))
		ERROR_RETURN_LOG(int, "Cannot define the data input pipe");

	if(NULL == (ctx->output = (pipe_t*)malloc(sizeof(ctx->output[0]) * (ctx->ncond + 1))))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the output array");

	uint32_t i;
	for(i = 0; i < ctx->ncond; i ++)
	{
		if(ERROR_CODE(pipe_t) == (ctx->output[i] = pipe_define_pattern("out%u", PIPE_MAKE_SHADOW(ctx->data) | PIPE_DISABLED, "$Tdata", i)))
			ERROR_LOG_GOTO(ERR, "Cannot define the output pipe");
		/* TODO: process the pattern */
	}

	if(ERROR_CODE(pipe_t) == (ctx->output[ctx->ncond] = pipe_define("default", PIPE_MAKE_SHADOW(ctx->data) | PIPE_DISABLED, "$Tdata")))
		ERROR_LOG_GOTO(ERR, "Cannot define the default output pipe");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_LOG_GOTO(ERR, "Cannot create type model");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->cond_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->cond, ctx->field)))
		ERROR_LOG_GOTO(ERR, "Cannot get the accessor for the input type");

	return 0;
ERR:
	if(ctx->output != NULL) free(ctx->output);
	if(ctx->type_model != NULL) pstd_type_model_free(ctx->type_model);

	return ERROR_CODE(int);
}

static inline int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	if(NULL != ctx->output) free(ctx->output);
	if(NULL != ctx->type_model) return pstd_type_model_free(ctx->type_model);

	return 0;
}

SERVLET_DEF = {
	.desc = "The demultiplexer, which takes N inputs and one condition, produces the copy of selected input",
	.version = 0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _unload
};
