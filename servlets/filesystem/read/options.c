/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>

static int _set_string(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;

	if(data.param_array_size < 1)
		ERROR_RETURN_LOG(int, "Wrong number of option argument");

	char** target = NULL;

	switch(data.current_option->short_opt)
	{
		case 'r':
			target  = &opt->root;
			break;
		case 'm':
			target = &opt->mime_spec;
			break;
		case 'f':
			target = &opt->forbiden_page;
			break;
		case 'n':
			target = &opt->not_found_page;
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid option: %c", data.current_option->short_opt);
	}

	if(*target != NULL) free(*target);

	if(NULL == (*target = strdup(data.param_array[0].strval)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot set the string value");

	return 0;
}

static int _set_switch(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;

	switch(data.current_option->short_opt)
	{
		case 'i':
			opt->inscure = 1;
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid option: %c", data.current_option->short_opt);
	}

	return 0;
}

static int _set_output_mode(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;

	if(data.current_option->short_opt != 'O')
		ERROR_RETURN_LOG(int, "Unexpected option");

	if(data.param_array_size < 1)
		ERROR_RETURN_LOG(int, "Wrong number of option arguments");

	const char* mode_str = data.param_array[0].strval;

	if(strcmp(mode_str, "raw") == 0)
		opt->output_mode = OPTIONS_OUTPUT_RAW;
	else if(strcmp(mode_str, "file") == 0)
		opt->output_mode = OPTIONS_OUTPUT_FILE;
	else if(strcmp(mode_str, "http") == 0)
		opt->output_mode = OPTIONS_OUTPUT_HTTP;
	else
		ERROR_RETURN_LOG(int, "Invalid mode string");
	
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
		.long_opt    = "root",
		.short_opt   = 'r',
		.pattern     = "S",
		.description = "Set the root directory",
		.handler     = _set_string,
		.args        = NULL  
	},
	{
		.long_opt    = "mime",
		.short_opt   = 'm',
		.pattern     = "S",
		.description = "The path to mime.types file",
		.handler     = _set_string,
		.args        = NULL  
	},
	{
		.long_opt    = "403",
		.short_opt   = 'f',
		.pattern     = "S",
		.description = "The path to 403 error page",
		.handler     = _set_string,
		.args        = NULL  
	},
	{
		.long_opt    = "404",
		.short_opt   = 'n',
		.pattern     = "S",
		.description = "The path to 404 error page",
		.handler     = _set_string,
		.args        = NULL  
	},
	{
		.long_opt    = "output-mode",
		.short_opt   = 'O',
		.pattern     = "S",
		.description = "Set the output mode, possible value: raw, file, http",
		.handler     = _set_output_mode,
		.args        = NULL  
	},
	{
		.long_opt    = "inscure",
		.short_opt   = 'i',
		.pattern     = "",
		.description = "Enable the inscure mode",
		.handler     = _set_switch,
		.args        = NULL  
	},
};

int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{
	if(NULL == argv || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == pstd_option_sort(_opts, sizeof(_opts) / sizeof(_opts[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the option");

	memset(buf, 0, sizeof(*buf));

	uint32_t ret = pstd_option_parse(_opts, sizeof(_opts) / sizeof(_opts[0]), argc, argv, buf);

	if(ERROR_CODE(uint32_t) == ret)
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	if(NULL == buf->root)
		ERROR_RETURN_LOG(int, "Missing --root");

	return 0;
}

int options_free(const options_t* opt)
{
	if(NULL != opt->root)
		free(opt->root);

	if(NULL != opt->mime_spec)
		free(opt->mime_spec);

	if(NULL != opt->not_found_page)
		free(opt->not_found_page);

	if(NULL != opt->forbiden_page)
		free(opt->forbiden_page);

	return 0;
}
