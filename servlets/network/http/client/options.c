/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pstd.h>

#include <options.h>


static inline int _opt_callback(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;
	switch(data.current_option->short_opt)
	{
		case 'T':
			opt->num_threads = (uint32_t)data.param_array[0].intval;
			break;
		case 'Q':
			opt->queue_size  = (uint32_t)data.param_array[0].intval;
			break;
		case 'P':
			opt->num_parallel = (uint32_t)data.param_array[0].intval;
			break;
		case 'H':
			opt->save_header = 1;
			break;
		case 'f':
			opt->follow_redir = 1;
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
		.long_opt    = "nthreads",
		.short_opt   = 'T',
		.pattern     = "I",
		.description = "Set the number of client threads can be used by the servlet [default value: 1]",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "parallel",
		.short_opt   = 'P',
		.pattern     = "I",
		.description = "Set the number of parallel request a thread can handle [default value: 128]",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "queue-size",
		.short_opt   = 'Q',
		.pattern     = "I",
		.description = "Set the maximum size of the request queue [default value: 1024]",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "save-header",
		.short_opt   = 'H',
		.pattern     = "",
		.description = "Indicates we need to save the header as well",
		.handler     = _opt_callback,
		.args        = NULL
	},
	{
		.long_opt    = "follow-redir",
		.short_opt   = 'f',
		.pattern     = "",
		.description = "Indicates we need to follow the redirection",
		.handler     = _opt_callback,
		.args        = NULL
	}
};


int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{

	if(ERROR_CODE(uint32_t) == argc || NULL == argv || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	buf->num_threads  = 1;
	buf->num_parallel = 128;
	buf->queue_size   = 1024;
	buf->save_header  = 0;
	buf->follow_redir = 0;

	if(ERROR_CODE(int) == pstd_option_sort(_opts, sizeof(_opts) / sizeof(_opts[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the options array");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_opts, sizeof(_opts) / sizeof(_opts[0]), argc, argv, buf))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet initialization string");

	return 0;
}
