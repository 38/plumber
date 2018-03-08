/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <mime.h>
#include <options.h>

static int _set_mode(pstd_option_data_t data)
{
	options_t* options = (options_t*)data.cb_data;

	if(data.param_array_size < 1)
	    ERROR_RETURN_LOG(int, "Unexpected number of parameters");

	const char* value = data.param_array[0].strval;

	if(data.current_option->short_opt == 'I')
	{
		if(strcmp(value, "raw") == 0)
		    options->input_mode = OPTIONS_INPUT_MODE_RAW;
		else if(strcmp(value, "string") == 0)
		    options->input_mode = OPTIONS_INPUT_MODE_STRING;
		else if(strncmp(value, "field=", 6) == 0)
		{
			options->input_mode = OPTIONS_INPUT_MODE_STRING_FIELD;
			if(NULL == (options->path_field = strdup(value + 6)))
			    ERROR_RETURN_LOG_ERRNO(int, "Cannot set the path field expression");
		}
		else goto INVALID;
	}
	else if(data.current_option->short_opt == 'O')
	{
		if(strcmp(value, "raw") == 0)
		    options->output_mode = OPTIONS_OUTPUT_MODE_RAW;
		else if(strcmp(value, "file") == 0)
		    options->output_mode = OPTIONS_OUTPUT_MODE_FILE;
		else if(strcmp(value, "http") == 0)
		    options->output_mode = OPTIONS_OUTPUT_MODE_HTTP;
		else goto INVALID;
	}

	return 0;
INVALID:
	ERROR_RETURN_LOG(int, "Invalid option: %c", data.current_option->short_opt);
}

static int _set_string_option(pstd_option_data_t data)
{
	options_t* options = (options_t*)data.cb_data;

	if(data.param_array_size < 1)
	    ERROR_RETURN_LOG(int, "Unexpected number of parameters");

	char** target = NULL;

	switch(data.current_option->short_opt)
	{
		case 'r':
		    target = &options->root_dir;
		    break;
		case 'm':
		    target = &options->mime_map_file;
		    break;
		case 'D':
		    target = &options->default_mime_type;
		    break;
		case 'C':
		    target = &options->compressable_types;
		    break;
		case 'N':
		    target = &options->http_err_not_found.filename;
		    break;
		case 'F':
		    target = &options->http_err_forbiden.filename;
		    break;
		case 'M':
		    target = &options->http_err_moved.filename;
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid options: %c", data.current_option->short_opt);
	}

	if(*target != NULL) return 0;

	if(NULL == (*target = strdup(data.param_array[0].strval)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot set the string value");

	return 0;
}

static int _set_bool_opt(pstd_option_data_t data)
{
	options_t* options = (options_t*)data.cb_data;

	switch(data.current_option->short_opt)
	{
		case 'd':
		    options->directory_list_page = 1;
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid arguments");
	}

	return 0;
}
static int _set_default_page_name(pstd_option_data_t data)
{
	options_t* options = (options_t*)data.cb_data;

	if(data.param_array_size < 1)
	    ERROR_RETURN_LOG(int, "Unexpected number of parameters");

	if(options->index_file_names != NULL) return 0;

	const char* value = data.param_array[0].strval;

	const char* begin, *end;
	uint32_t count = 0, i = 0;

	for(begin = end = value;; end ++)
	{
		if(*end == ',' || *end == 0)
		{
			if(end - begin > 0) count ++;
			begin = end + 1;
		}

		if(*end == 0) break;
	}

	if(NULL == (options->index_file_names = (char**)calloc(sizeof(char*), count + 1)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the index file names array");

	for(begin = end = value;; end ++)
	{
		if(*end == ',' || *end == 0)
		{
			if(end - begin > 0)
			{
				if(NULL == (options->index_file_names[i] = (char*)malloc((size_t)(end - begin + 1))))
				    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the index file names array");

				memcpy(options->index_file_names[i], begin, (size_t)(end - begin));
				options->index_file_names[i][end - begin] = 0;
				i ++;
			}
			begin = end + 1;

			if(*end == 0) break;
		}
	}

	return 0;
ERR:
	for(i = 0; i < count; i ++)
	    if(NULL != options->index_file_names[i])
	        free(options->index_file_names[i]);
	free(options->index_file_names);
	return ERROR_CODE(int);
}

static pstd_option_t _opts[] = {
	{
		.long_opt       = "help",
		.short_opt      = 'h',
		.description    = "Show this help message",
		.pattern        = "",
		.handler        = pstd_option_handler_print_help,
		.args           = NULL
	},
	{
		.long_opt       = "input-mode",
		.short_opt      = 'I',
		.description    = "Specify the input mode, possible values: [raw, string, field=<field-expr>]",
		.pattern        = "S",
		.handler        = _set_mode,
		.args           = NULL
	},
	{
		.long_opt       = "output-mode",
		.short_opt      = 'O',
		.description    = "Specify the output mode, possible values: [raw, file, http]",
		.pattern        = "S",
		.handler        = _set_mode,
		.args           = NULL
	},
	{
		.long_opt       = "root",
		.short_opt      = 'r',
		.description    = "Sepcify the root directory (Required)",
		.pattern        = "S",
		.handler        = _set_string_option,
		.args           = NULL
	},
	{
		.long_opt       = "not-found-page",
		.short_opt      = 'N',
		.description    = "Sepcify the path to the customized not found error page",
		.pattern        = "S",
		.handler        = _set_string_option,
		.args           = NULL
	},
	{
		.long_opt       = "forbiden-page",
		.short_opt      = 'F',
		.description    = "Sepcify the path to the customized forbiden error page",
		.pattern        = "S",
		.handler        = _set_string_option,
		.args           = NULL
	},
	{
		.long_opt       = "moved-page",
		.short_opt      = 'M',
		.description    = "Sepcify the path to the customized moved page",
		.pattern        = "S",
		.handler        = _set_string_option,
		.args           = NULL
	},
	{
		.long_opt       = "default-mime-type",
		.short_opt      = 'D',
		.description    = "Sepcify the default MIME type",
		.pattern        = "S",
		.handler        = _set_string_option,
		.args           = NULL
	},
	{
		.long_opt       = "mime-map-file",
		.short_opt      = 'm',
		.description    = "Sepcify the MIME map file",
		.pattern        = "S",
		.handler        = _set_string_option,
		.args           = NULL
	},
	{
		.long_opt       = "compressable",
		.short_opt      = 'C',
		.description    = "Sepcify the wildcard list of compressable MIME types",
		.pattern        = "S",
		.handler        = _set_string_option,
		.args           = NULL
	},
	{
		.long_opt       = "index",
		.short_opt      = 'i',
		.description    = "Sepcify the list of index file names",
		.pattern        = "S",
		.handler        = _set_default_page_name,
		.args           = NULL
	},
	{
		.long_opt       = "default-index",
		.short_opt      = 'd',
		.description    = "Enable the default index page",
		.pattern        = "",
		.handler        = _set_bool_opt,
		.args           = NULL
	}
};

static inline int _init_error_page(const mime_map_t* map, options_output_err_page_t* page)
{
	mime_map_info_t info;

	const char* extname = NULL, *ptr;

	if(page->filename == NULL)
	    return 0;

	for(ptr = page->filename; *ptr; ptr ++)
	    if(*ptr == '.') extname = ptr + 1;

	if(ERROR_CODE(int) == mime_map_query(map, extname, &info))
	    ERROR_RETURN_LOG(int, "Cannot query the MIME type map");

	page->mime_type = info.mime_type;
	page->compressable = info.compressable;

	return 0;
}

int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{
	if(NULL == argv || NULL == buf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	memset(buf, 0, sizeof(options_t));

	if(ERROR_CODE(int) == pstd_option_sort(_opts, sizeof(_opts) / sizeof(_opts[0])))
	    ERROR_RETURN_LOG(int, "Cannot sort the opts array");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_opts, sizeof(_opts) / sizeof(_opts[0]), argc, argv, buf))
	    ERROR_RETURN_LOG(int, "Cannot parse the servlet initialization string");

	if(buf->root_dir == NULL)
	    ERROR_RETURN_LOG(int, "Missing --root");

	if(buf->output_mode == OPTIONS_OUTPUT_MODE_HTTP)
	{
		if(NULL == (buf->mime_map = mime_map_new(buf->mime_map_file, buf->compressable_types, buf->default_mime_type)))
		    ERROR_RETURN_LOG(int, "Cannot load the MIME type map");

		if(ERROR_CODE(int) == _init_error_page(buf->mime_map, &buf->http_err_not_found))
		    ERROR_RETURN_LOG(int, "Cannot initialize the 404 error page");

		if(ERROR_CODE(int) == _init_error_page(buf->mime_map, &buf->http_err_forbiden))
		    ERROR_RETURN_LOG(int, "Cannot initialize the 405 error page");

		if(ERROR_CODE(int) == _init_error_page(buf->mime_map, &buf->http_err_moved))
		    ERROR_RETURN_LOG(int, "Cannot initialize the 301 page");

		if(NULL == buf->index_file_names)
		{
			if(NULL == (buf->index_file_names = (char**)calloc(sizeof(char*), 3)))
			    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the default index filenames");

			if(NULL == (buf->index_file_names[0] = strdup("index.html")))
			    ERROR_RETURN_LOG_ERRNO(int, "Cannot append index.html to the default index filename list");

			if(NULL == (buf->index_file_names[1] = strdup("index.htm")))
			    ERROR_RETURN_LOG_ERRNO(int, "Cannot append index.htm to the default index filename list");
		}
	}

	return 0;
}

int options_free(const options_t* options)
{
	if(NULL != options->path_field) free(options->path_field);

	if(NULL != options->http_err_not_found.filename) free(options->http_err_not_found.filename);
	if(NULL != options->http_err_forbiden.filename) free(options->http_err_forbiden.filename);
	if(NULL != options->http_err_moved.filename) free(options->http_err_moved.filename);

	if(NULL != options->default_mime_type) free(options->default_mime_type);
	if(NULL != options->compressable_types) free(options->compressable_types);
	if(NULL != options->mime_map_file) free(options->mime_map_file);

	if(NULL != options->root_dir) free(options->root_dir);

	if(NULL != options->index_file_names)
	{
		uint32_t i;
		for(i = 0; options->index_file_names[i]; i ++)
		    free(options->index_file_names[i]);
		free(options->index_file_names);
	}

	int rc = 0;

	if(options->mime_map != NULL && ERROR_CODE(int) == mime_map_free(options->mime_map))
	    rc = ERROR_CODE(int);

	return rc;
}
