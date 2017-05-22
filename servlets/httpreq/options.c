/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <error.h>
#include <pservlet.h>
#include <pstd.h>

#include <httpreq.h>
#include <options.h>

static inline int _show_version(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* userdata)
{
	(void)idx;
	(void)params;
	(void)nparams;
	(void)options;
	(void)n;
	(void)userdata;
	fprintf(stderr, "HTTP Request Parser Version: 0.0.0\n");
	return 0;
}

static inline int _produce_output(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* userdata)
{
	(void)n;
	char pipe_name = options[idx].short_opt;
	httpreq_options_t* buffer = (httpreq_options_t*)userdata;

	uint32_t val = 1;

	if(nparams > 0 && params[0].type == PSTD_OPTION_TYPE_INT)
	{
		int64_t value = params[0].intval;
		if(value != 0) val = 1;
		else val = 0;
	}

	switch(pipe_name)
	{
		case 'c':
		    buffer->produce_cookie = (val != 0);
		    break;
		case 'm':
		    buffer->produce_method = (val != 0);
		    break;
		case 'H':
		    buffer->produce_host = (val != 0);
		    break;
		case 'p':
		    buffer->produce_path = (val != 0);
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid argument def");
	}

	return 0;
}

static inline int _allowed_method(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* userdata)
{
	(void)idx;
	(void)options;
	(void)n;
	uint32_t i;
	httpreq_options_t* buffer = (httpreq_options_t*)userdata;

	for(i = 0; i < nparams; i ++)
	{
		char buf[32];
		uint32_t pptr = 0, bptr = 0;
#define _IS(name) else if(params[i].type == PSTD_OPTION_STRING && strcmp(#name, buf) == 0) \
		do {\
			buffer->method_allowed |= (1ull << HTTPREQ_VERB_##name);\
			bptr = 0;\
			if(ch == 0) goto RET;\
		} while(0)
		for(;;)
		{
			char ch = params[i].strval[pptr++];
			if(ch != ',' && ch != 0)
			{
				if(bptr < sizeof(buf)) buf[bptr++] = ch, buf[bptr] = 0;
			}
			_IS(OPTIONS);
			_IS(GET);
			_IS(HEAD);
			_IS(POST);
			_IS(PUT);
			_IS(DELETE);
			_IS(TRACE);
			_IS(CONNECT);
			else ERROR_RETURN_LOG(int, "Invalid method name %s", buf);
		}
#undef _IS
	}
RET:
	return 0;
}
static inline int _enable_text_output(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* userdata)
{
	(void)idx;
	(void)options;
	(void)n;
	(void)params;
	(void)nparams;
	httpreq_options_t* buffer = (httpreq_options_t*)userdata;

	buffer->text_output = 1;

	return 0;
}

static pstd_option_t _httpreq_options[] = {
	{
		.long_opt = "help",
		.short_opt = 'h',
		.pattern = "",
		.description = "Print this help message",
		.handler = pstd_option_handler_print_help,
		.args = NULL
	},
	{
		.long_opt = "version",
		.short_opt = 'v',
		.pattern = "",
		.description = "Show version number",
		.handler = _show_version,
		.args = NULL
	},
	{
		.long_opt = "method-allowed",
		.short_opt = 'M',
		.pattern = "S",
		.description = "Select with method to support",
		.handler = _allowed_method,
		.args = NULL
	},
	{
		.long_opt = "host",
		.short_opt = 'H',
		.pattern = "?I",
		.description = "If the parser produces hostname pipe",
		.handler = _produce_output,
		.args = NULL
	},
	{
		.long_opt = "method",
		.short_opt = 'm',
		.pattern = "?I",
		.description = "If the parser produces method pipe",
		.handler = _produce_output,
		.args = NULL
	},
	{
		.long_opt = "path",
		.short_opt = 'p',
		.pattern = "?I",
		.description = "If the parser produces path pipe",
		.handler = _produce_output,
		.args = NULL
	},
	{
		.long_opt = "cookie",
		.short_opt = 'c',
		.pattern = "?I",
		.description = "If the parser produces method pipe",
		.handler = _produce_output,
		.args = NULL
	},
	{
		.long_opt = "text",
		.short_opt = 'T',
		.pattern = "",
		.description = "Indicates the servlet should output plain text output",
		.handler = _enable_text_output,
		.args = NULL
	}
};

httpreq_options_t* httpreq_options_parse(uint32_t argc, char const * const * argv)
{
	if(NULL == argv || argc <= 0) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(ERROR_CODE(int) == pstd_option_sort(_httpreq_options, sizeof(_httpreq_options) / sizeof(_httpreq_options[0])))
	    ERROR_PTR_RETURN_LOG("Cannot sort the options array");

	httpreq_options_t* ret = (httpreq_options_t*)calloc(1, sizeof(httpreq_options_t));

	if(NULL == ret) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the HTTP request servlet options");
	uint32_t rc;

	if((rc = pstd_option_parse(_httpreq_options, sizeof(_httpreq_options) / sizeof(_httpreq_options[0]), argc, argv, ret)) == ERROR_CODE(uint32_t))
	    ERROR_LOG_GOTO(ERR, "Cannot parse the command line arguments");
	if(rc != argc) LOG_WARNING("Commandline arguments is not exhuasted");

	goto RET;
ERR:
	if(NULL != ret) httpreq_options_free(ret);
	ret = NULL;
RET:
	return ret;
}

int httpreq_options_free(httpreq_options_t* options)
{
	if(NULL == options) ERROR_RETURN_LOG(int, "Invalid arguments");

	free(options);

	return 0;
}
