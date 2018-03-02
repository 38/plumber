/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>

static inline int _opt_callback(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;
	switch(data.current_option->short_opt)
	{
		case 'g':
			opt->gzip = 1;
			opt->chuncked = 1;
		    break;
		case 'c':
			opt->compress = 1;
			opt->chuncked = 1;
		    break;
		case 'd':
			opt->deflate = 1;
			opt->chuncked = 1;
		    break;
		case 'b':
			opt->br = 1;
			opt->chuncked = 1;
		    break;
		case 'C':
			opt->chuncked = 1;
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid options");
	}

	return 0;
}

static pstd_option_t _opts[] = {
	{
		.long_opt    = "help",
		.short_opt   = 'h',
		.pattern     = "",
		.description = "Show this help message",
		.handler     = pstd_option_handler_print_help,
		.args        = NULL
	},
	{
		.long_opt    = "gzip",
		.short_opt   = 'g',
		.pattern     = "",
		.description = "Use gzip compression",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "compress",
		.short_opt   = 'c',
		.pattern     = "",
		.description = "Use compress compression",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "deflate",
		.short_opt   = 'd',
		.pattern     = "",
		.description = "Use deflate compression",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "br",
		.short_opt   = 'b',
		.pattern     = "",
		.description = "Use BR compression",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "chuncked",
		.short_opt   = 'C',
		.pattern     = "",
		.description = "Use chuncked compression",
		.handler     = _opt_callback,
		.args        = NULL
	}
};

int options_parse(uint32_t argc, char const* const* argv, options_t* buffer)
{
	if(ERROR_CODE(uint32_t) == argc || NULL == argv || NULL == buffer)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	memset(buffer, 0, sizeof(options_t));

	if(ERROR_CODE(int) == pstd_option_sort(_opts, sizeof(_opts) / sizeof(_opts[0])))
	    ERROR_RETURN_LOG(int, "Cannot sort the options array");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_opts, sizeof(_opts) / sizeof(_opts[0]), argc, argv, buffer))
	    ERROR_RETURN_LOG(int, "Cannot parse the servlet initialization string");

	return 0;
}
