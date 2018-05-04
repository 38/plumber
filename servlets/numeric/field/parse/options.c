/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <utils/static_assertion.h>

#include <pservlet.h>
#include <pstd.h>
#include <options.h>

/**
 * @brief The type name of the output field
 **/
static char const* const _output_type_name[] = {
	[OPTIONS_CELL_TYPE_DOUBLE] = "plumber/std/numeric/DoubleField"
};

static char const* const _cell_type_name[] = {
	[OPTIONS_CELL_TYPE_DOUBLE] = "double"
};
STATIC_ASSERTION_EQ(OPTIONS_CELL_TYPE_COUNT, sizeof(_output_type_name) / sizeof(_output_type_name[0]));
STATIC_ASSERTION_EQ_ID(check_name_array_size, sizeof(_output_type_name), sizeof(_cell_type_name));

static int _parse_type(pstd_option_data_t data)
{
	if(data.param_array_size != 1)
		ERROR_RETURN_LOG(int, "Invalid number of arguments");

	options_t* opt = (options_t*)data.cb_data;

	uint32_t i;
	for(i = 0; i < OPTIONS_CELL_TYPE_COUNT; i ++)
		if(strcmp(data.param_array[0].strval, _cell_type_name[i]) == 0)
		{
			opt->cell_type = (options_cell_type_t)OPTIONS_CELL_TYPE_COUNT;
			return 0;
		}

	return ERROR_CODE(int);
}

static inline  uint32_t _split(const char* str, const char* delim, char const* * buf, uint32_t size)
{
	const char* ptr;
	const char* state = delim;
	uint32_t ret = 0;

	buf[ret++] = str;

	for(ptr = str; *ptr; ptr ++)
	{
		if(*ptr == *state)
		{
			state ++;
			if(*state == 0)
			{
				if(ret >= size)
					ERROR_RETURN_LOG(uint32_t, "Too many sections");

				buf[ret++] = ptr + 1;
				state = delim; 
			}
		}
		else
		{
			ptr -= (state - delim);
			state = delim;
		}
	}

	return ret;
}

static inline const void* memmem(const void* h, size_t hs, const void* n, size_t ns)
{
	const char* limit = ((const char*)h) + hs;
	const char* start = (const char*)memchr(h, *(const char*)n, hs);

	if(start == NULL) return NULL;

	for(;start + ns < limit && memcmp(start, n, ns); start ++);

	if(start + ns >= limit) return NULL;

	return start;
}

static int _parse_ndim(pstd_option_data_t data)
{
	if(data.param_array_size != 1)
		ERROR_RETURN_LOG(int, "Invalid number of arguments");

	options_t* opt = (options_t*)data.cb_data;

	opt->n_dim = (uint32_t)data.param_array[0].intval;

	return 0;
}

static int _parse_dim_val(pstd_option_data_t data)
{
	if(data.param_array_size != 1)
		ERROR_RETURN_LOG(int, "Invalid number of arguments");

	options_t* opt = (options_t*)data.cb_data;

	if(opt->dim_data != NULL)
		ERROR_RETURN_LOG(int, "Only one --dim param is allowed");

	const char* sect[32];  /* TODO: magic number */

	uint32_t split_rc = _split(data.param_array[0].strval, ",", sect, sizeof(sect) / sizeof(sect[0]));

	if(ERROR_CODE(uint32_t) == split_rc)
		ERROR_RETURN_LOG(int, "Cannot split the dimension array");

	uint32_t dim_size = psnl_dim_data_size_nd(split_rc);
	if(NULL == (opt->dim_data = (psnl_dim_t*)calloc(1, dim_size)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the dimension");

	opt->dim_data->n_dim = split_rc;

	uint32_t i;
	for(i = 0; i < split_rc; i ++)
	{
		const char* begin = sect[i];
		const char* end   = i + 1 < split_rc ? sect[i + 1] : sect[i] + strlen(sect[i]);
		const char* mid = (const char*)memmem(begin, (size_t)(end - begin), "..", 2);

		if(NULL == mid)
		{
			char* endpos = NULL;
			long long val = strtoll(begin, &endpos, 0);

			if(NULL == endpos || (endpos[0] != 0 && endpos + 1 != end))
				ERROR_LOG_GOTO(ERR, "Invalid integer");

			opt->dim_data->dims[i][0] = 0;
			opt->dim_data->dims[i][1] = (int32_t)val;
		}
		else
		{
			char* endpos = NULL;
			long long val = strtoll(begin, &endpos, 0);

			if(NULL == endpos || endpos != mid)
				ERROR_LOG_GOTO(ERR, "Invalid integer");

			opt->dim_data->dims[i][0] = (int32_t)val;

			val = strtoll(mid + 2, &endpos, 0);
			if(NULL == endpos || (endpos[0] != 0 && endpos + 1 != end))
				ERROR_LOG_GOTO(ERR, "Invalid integer");

			opt->dim_data->dims[i][1] = (int32_t)val;
		}

		if(opt->dim_data->dims[i][0] > opt->dim_data->dims[i][1])
			ERROR_LOG_GOTO(ERR, "Invalid range %d..%d", opt->dim_data->dims[i][0], opt->dim_data->dims[i][1]);
	}

	return 0;
ERR:

	if(NULL != opt->dim_data)
		free(opt->dim_data);
	opt->dim_data = NULL;
	return ERROR_CODE(int);
}

static int _parse_switch(pstd_option_data_t data)
{
	options_t* opt = (options_t*)data.cb_data;

	switch(data.current_option->short_opt)
	{
		case 'b':
			opt->in_format = OPTIONS_INPUT_FORMAT_BINARY;
			break;
		case 'r':
			opt->raw = 1;
			break;
	}

	return 0;
}

/**
 * @brief The actual options
 **/
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
		.long_opt    = "cell-type",
		.short_opt   = 'T',
		.pattern     = "S",
		.description = "Set the type of the cell in the field",
		.handler     = _parse_type,
		.args        = NULL
	},
	{
		.long_opt    = "ndim",
		.short_opt   = 'n',
		.pattern     = "I",
		.description = "Set the number of the dimension (required)",
		.handler     = _parse_ndim,
		.args        = NULL
	},
	{
		.long_opt    = "dim",
		.short_opt   = 'd',
		.pattern     = "S",
		.description = "Set the dimension size, e.g. --dim-val 5,5 or --dim-val -3..10,-10..10",
		.handler     = _parse_dim_val,
		.args        = NULL
	},
	{
		.long_opt    = "binary",
		.short_opt   = 'b',
		.pattern     = "",
		.description = "Set the input model to binary mode",
		.handler     = _parse_switch,
		.args        = NULL
	},
	{
		.long_opt    = "raw",
		.short_opt   = 'r',
		.pattern     = "",
		.description = "Make the servlet read from a raw pipe port, otherwise the input assume to be string",
		.handler     = _parse_switch,
		.args        = NULL
	}
};

int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{
	if(NULL == argv || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	memset(buf, 0, sizeof(*buf));

	if(ERROR_CODE(int) == pstd_option_sort(_opts, sizeof(_opts) / sizeof(_opts[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the servlet init option templates");

	buf->in_format = OPTIONS_INPUT_FORMAT_STRING;
	buf->raw = 0;
	buf->n_dim = ERROR_CODE(uint32_t);
	buf->cell_type = OPTIONS_CELL_TYPE_DOUBLE;

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_opts, sizeof(_opts) / sizeof(_opts[0]), argc, argv, buf))
		ERROR_LOG_GOTO(ERR, "Cannot parse the options");

	if(buf->n_dim == ERROR_CODE(uint32_t) && NULL == buf->dim_data)
		ERROR_LOG_GOTO(ERR, "Either dimension data or number of dimensions should be specified");

	if(buf->n_dim != ERROR_CODE(uint32_t) && 
	   buf->dim_data != NULL && buf->n_dim != buf->dim_data->n_dim)
		ERROR_LOG_GOTO(ERR, "Conflict --ndim and --dim");

	if(buf->n_dim == ERROR_CODE(uint32_t))
		buf->n_dim = buf->dim_data->n_dim;

	if(buf->raw)
		buf->input_type = "plumber/base/Raw";
	else
		buf->input_type = "plumber/std/request_local/String";

	size_t result_size = strlen(_output_type_name[buf->cell_type]) + 32;
	if(NULL == (buf->result_type = (char*)malloc(result_size)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the result type");

	snprintf(buf->result_type, result_size, "%s @dim(%u)", _output_type_name[buf->cell_type], buf->n_dim);

	return 0;

ERR:
	options_free(buf);
	return ERROR_CODE(int);
}

int options_free(options_t* opt)
{
	if(NULL == opt)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != opt->result_type) free(opt->result_type);
	if(NULL != opt->dim_data) free(opt->dim_data);

	return 0;
}
