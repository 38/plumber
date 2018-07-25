/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pstd.h>
#include <options.h>

static int _opt_handle(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;
	switch(data.current_option->short_opt)
	{
		case 'p':
			opt->conn_pool_size = (uint32_t)data.param_array[0].intval;
			goto OPT_CHK;
		case 'P':
			opt->conn_per_peer = (uint32_t)data.param_array[0].intval;
			goto OPT_CHK;
		case 'T':
			opt->conn_timeout = (uint32_t)data.param_array[0].intval;
OPT_CHK:
			if(data.param_array[0].intval < 0)
				ERROR_RETURN_LOG(int, "Invalid parameter");
			break;
		default:
			ERROR_RETURN_LOG(int, "Unrecoginized options");
	}

	return 0;
}

static pstd_option_t _options[] = {
	{
		.long_opt    = "help",
		.short_opt   = 'h',
		.description = "Show this help message",
		.pattern     = "",
		.handler     = pstd_option_handler_print_help,
		.args        = NULL
	},
	{
		.long_opt    = "pool-size",
		.short_opt   = 'p',
		.description = "The connection pool size",
		.pattern     = "I",
		.handler     = _opt_handle,
		.args        = NULL
	},
	{
		.long_opt    = "peer-pool-size",
		.short_opt   = 'P',
		.description = "The maximum number of connection that can be perserved per peer",
		.pattern     = "I",
		.handler     = _opt_handle,
		.args        = NULL
	},
	{
		.long_opt    = "timeout",
		.short_opt   = 'T',
		.description = "The amount of time the socket can wait for data",
		.pattern     = "I",
		.handler     = _opt_handle,
		.args        = NULL
	}
};

int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{
	if(NULL == argv || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	buf->conn_pool_size = 1024;
	buf->conn_per_peer = 32;
	buf->conn_timeout = 30;

	if(ERROR_CODE(int) == pstd_option_sort(_options, sizeof(_options) / sizeof(_options[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the options");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_options, sizeof(_options) / sizeof(_options[0]), argc, argv, buf))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init stirng");

	return 0;
}
