/**
 * Copyright (C) 2018, Hao Hou
 **/
#include  <string.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>

static int _switch(pstd_option_data_t data)
{
	options_t* buf = (options_t*)data.cb_data;

	switch(data.current_option->short_opt)
	{
		case 'D':
			buf->dump_dim = 1;
			break;
		case 'S':
			buf->slice_coord = 1;
			break;
		case 'B':
			buf->binary = 1;
			break;
		default:
			ERROR_RETURN_LOG(int, "Code bug: Invalid switch");
	}
	return 0;
}

static pstd_option_t _opts[] = {
	{
		.long_opt    = "help",
		.short_opt   = 'h',
		.description = "Show this message",
		.pattern     = "",
		.handler     = pstd_option_handler_print_help,
		.args        = NULL
	},
	{
		.long_opt    = "dump-dim",
		.short_opt   = 'D',
		.description = "Dump the dimension data of the field",
		.pattern     = "",
		.handler     = _switch,
		.args        = NULL
	},
	{
		.long_opt    = "slice",
		.short_opt   = 'S',
		.description = "Dump the coordiante of the 2D slice",
		.pattern     = "",
		.handler     = _switch,
		.args        = NULL
	},
	{
		.long_opt    = "binary",
		.short_opt   = 'B',
		.description = "Dump the field in binary field",
		.pattern     = "",
		.handler    = _switch,
		.args        = NULL
	}
};

int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{
	if(NULL == argv || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	memset(buf, 0, sizeof(options_t));

	if(ERROR_CODE(int) == pstd_option_sort(_opts, sizeof(_opts) / sizeof(_opts[0])))
		ERROR_RETURN_LOG(int, "Cannot short the option template array");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_opts, sizeof(_opts) / sizeof(_opts[0]), argc, argv, buf))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init param");

	return 0;
}

int options_free(options_t* buf)
{
	if(NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	return 0;
}
