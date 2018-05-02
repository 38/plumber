/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <utils/static_assertion.h>

#include <pservlet.h>
#include <pstd.h>
#include <options.h>

/**
 * @brief The type name of the output field
 **/
static char const* const _output_type_name[] = {
	[OPTIONS_CELL_TYPE_DOUBLE] "plumber/std/numeric/DoubleField"
};

static char const* const _cell_type_name[] = {
	[OPTIONS_CELL_TYPE_DOUBLE] "double"
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

static inline  size_t _split(const char* str, const char* delim, char const* * buf, size_t size)
{
	const char* ptr;
	const char* state = delim;
	size_t ret = 0;

	buf[ret++] = str;

	for(ptr = str; *ptr; ptr ++)
	{
		if(*ptr == *state)
		{
			state ++;
			if(*state == 0)
			{
				if(ret >= size)
					ERROR_RETURN_LOG(size_t, "Too many sections");

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

static int _parse_ndim(pstd_option_data_t data)
{
	(void)data;
	return 0;
}

static int _parse_dim_val(pstd_option_data_t data)
{
	(void)data;
	return 0;
}

static int _parse_switch(pstd_option_data_t data)
{
	(void)data;
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
		.long_opt    = "num-dim",
		.short_opt   = 'n',
		.pattern     = "I",
		.description = "Set the number of the dimension (required)",
		.handler     = _parse_ndim,
		.args        = NULL
	},
	{
		.long_opt    = "dim-val",
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

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_opts, sizeof(_opts) / sizeof(_opts[0]), argc, argv, buf))
		ERROR_RETURN_LOG(int, "Cannot parse the options");

	return 0;
}
