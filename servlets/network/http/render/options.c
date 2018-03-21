/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <config.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>

static int _opt_callback_no_val(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;
	switch(data.current_option->short_opt)
	{
#ifdef HAS_ZLIB
		case 'g':
		    opt->gzip_enabled = 1;
		    opt->chunked_enabled = 1;
		    break;
		case 'd':
		    opt->deflate_enabled = 1;
		    opt->chunked_enabled = 1;
		    break;
#endif

#ifdef HAS_BROTLI
		case 'b':
		    opt->br_enabled = 1;
		    opt->chunked_enabled = 1;
		    break;
#endif
		case 'c':
		    opt->chunked_enabled = 1;
		    break;
		case 'P':
		    opt->reverse_proxy = 1;
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid option");
	}

	return 0;
}

static int _opt_callback_numeric(pstd_option_data_t data)
{
	if(data.param_array_size != 1)
	    ERROR_RETURN_LOG(int, "Invalid number of parameter");

	options_t* opt = (options_t*)data.cb_data;

	int64_t val = data.param_array[0].intval;

	switch(data.current_option->short_opt)
	{
		case 'L':
		    opt->compress_level = ((uint8_t)val & 0xfu);
		    break;
		case 'S':
		    opt->max_chunk_size = ((uint8_t)val & 0xffu);
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid option");
	}
	return 0;
}

static int _opt_callback_string(pstd_option_data_t data)
{
	if(data.param_array_size != 1)
	    ERROR_RETURN_LOG(int, "Invalid number of parameter");

	options_t* opt = (options_t*)data.cb_data;
	char** target = NULL;
	char* val = NULL;

	switch(data.current_option->short_opt)
	{
		case '4':
		    target = &opt->err_406.error_page;
		    break;
		case '5':
		    target = &opt->err_500.error_page;
		    break;
		case 'e':
		    target = &opt->err_503.error_page;
		    break;
		case 'b':
		    target = &opt->err_400.error_page;
		    break;
		case 0:
		    if(strcmp("500-mime", data.current_option->long_opt) == 0)
		        target = &opt->err_500.mime_type;
		    else if(strcmp("406-mime", data.current_option->long_opt) == 0)
		        target = &opt->err_406.mime_type;
		    else if(strcmp("503-mime", data.current_option->long_opt) == 0)
		        target = &opt->err_503.mime_type;
		    else if(strcmp("400-mime", data.current_option->long_opt) == 0)
		        target = &opt->err_400.mime_type;
		    else
		        ERROR_RETURN_LOG(int, "Invalid options");
		    break;
		case 's':
		{
			target = &opt->server_name;
			size_t len = strlen(data.param_array[0].strval) + sizeof("Server: \r\n");
			val = (char*)malloc(len);
			if(NULL != val)
				snprintf(val, len, "Server: %s\r\n", data.param_array[0].strval);
			goto FINISHED;
		}
		default:
		    ERROR_RETURN_LOG(int, "Invalid options");
	}

	val = strdup(data.param_array[0].strval);

FINISHED:
	if(NULL == val)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the param string");

	if(NULL != *target)
	    free(*target);

	*target = val;

	return 0;
}

static pstd_option_t _options[] = {
	{
		.long_opt    = "help",
		.short_opt   = 'h',
		.pattern     = "",
		.description = "Show this help message",
		.handler     = pstd_option_handler_print_help,
		.args        = NULL
	},
#ifdef HAS_ZLIB
	{
		.long_opt    = "gzip",
		.short_opt   = 'g',
		.pattern     = "",
		.description = "Enable gzip compression",
		.handler     = _opt_callback_no_val,
		.args        = NULL
	},
	{
		.long_opt    = "deflate",
		.short_opt   = 'd',
		.pattern     = "",
		.description = "Enable deflate compression",
		.handler     = _opt_callback_no_val,
		.args        = NULL
	},
#endif
#ifdef HAS_BROTLI
	{
		.long_opt    = "br",
		.short_opt   = 'b',
		.pattern     = "",
		.description = "Use BR compression",
		.handler     = _opt_callback_no_val,
		.args        = NULL
	},
#endif
	{
		.long_opt    = "chunked",
		.short_opt   = 'c',
		.pattern     = "",
		.description = "Enable Chunked Encoding",
		.handler     = _opt_callback_no_val,
		.args        = NULL
	},
	{
		.long_opt    = "compression-level",
		.short_opt   = 'L',
		.pattern     = "I",
		.description = "The compression level from 0 to 9",
		.handler     = _opt_callback_numeric,
		.args        = NULL
	},
	{
		.long_opt    = "chunk-size",
		.short_opt   = 'C',
		.pattern     = "I",
		.description = "The maximum chunk size in number of pages",
		.handler     = _opt_callback_numeric,
		.args        = NULL
	},
	{
		.long_opt    = "server-name",
		.short_opt   = 's',
		.pattern     = "S",
		.description = "What we need return for the server name field",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "500-page",
		.short_opt   = '5',
		.pattern     = "S",
		.description = "Server Internal Error page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "500-mime",
		.pattern     = "S",
		.description = "Type of Server Internal Error page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "406-page",
		.short_opt   = '4',
		.pattern     = "S",
		.description = "Not-Acceptable Error page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "406-mime",
		.pattern     = "S",
		.description = "Type of Not-Acceptable Error page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "503-page",
		.short_opt   = 'e',
		.pattern     = "S",
		.description = "Service Not Available Error page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "503-mime",
		.pattern     = "S",
		.description = "Type of Service Not Available Error page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "400-page",
		.short_opt   = 'b',
		.pattern     = "S",
		.description = "Bad Request Page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "400-mime",
		.pattern     = "S",
		.description = "Type of Bad Request Page",
		.handler     = _opt_callback_string,
		.args        = NULL
	},
	{
		.long_opt    = "proxy",
		.short_opt   = 'P',
		.pattern     = "",
		.description = "Enable the reverse proxy support",
		.handler     = _opt_callback_no_val,
		.args        = NULL
	}
};

int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{
	if(NULL == argv || NULL == buf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	memset(buf, 0, sizeof(options_t));

	buf->compress_level = 5;
	buf->max_chunk_size = 8;

	if(ERROR_CODE(int) == pstd_option_sort(_options, sizeof(_options) / sizeof(_options[0])))
	    ERROR_RETURN_LOG(int, "Cannot sort the options array");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_options, sizeof(_options) / sizeof(_options[0]), argc, argv, buf))
	    ERROR_RETURN_LOG(int, "Cannot parse the servlet initialization string");

	if(NULL == buf->server_name && NULL == (buf->server_name = strdup("Server: Plumber-PINS/"PLUMBER_VERSION_SHORT"\r\n")))
	    ERROR_RETURN_LOG(int, "Cannot initialize the default server name");

	if(NULL == buf->err_406.mime_type && NULL == (buf->err_406.mime_type = strdup("text/html")))
	    ERROR_RETURN_LOG(int, "Cannot initialize the default server name");

	if(NULL == buf->err_500.mime_type && NULL == (buf->err_500.mime_type = strdup("text/html")))
	    ERROR_RETURN_LOG(int, "Cannot initialize the default server name");

	if(NULL == buf->err_503.mime_type && NULL == (buf->err_503.mime_type = strdup("text/html")))
	    ERROR_RETURN_LOG(int, "Cannot initialize the default server name");

	if(NULL == buf->err_400.mime_type && NULL == (buf->err_400.mime_type = strdup("text/html")))
	    ERROR_RETURN_LOG(int, "Cannot initialize the default server name");

	return 0;
}

int options_free(const options_t* options)
{
	if(NULL == options)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != options->server_name) free(options->server_name);

	if(NULL != options->err_406.error_page) free(options->err_406.error_page);
	if(NULL != options->err_406.mime_type) free(options->err_406.mime_type);

	if(NULL != options->err_500.error_page) free(options->err_500.error_page);
	if(NULL != options->err_500.mime_type) free(options->err_500.mime_type);

	if(NULL != options->err_503.error_page) free(options->err_503.error_page);
	if(NULL != options->err_503.mime_type) free(options->err_503.mime_type);

	if(NULL != options->err_400.error_page) free(options->err_400.error_page);
	if(NULL != options->err_400.mime_type) free(options->err_400.mime_type);
	return 0;
}
