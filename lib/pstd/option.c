/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <option.h>
#include <error.h>
#include <utils/static_assertion.h>

/**
 * @brief the internal buffer used to parse the command line arguments
 **/
typedef struct {
	uint32_t capacity;
	uint32_t size;
	uintptr_t __padding__[0];
	pstd_option_param_t params[0];
} _buffer_t;
STATIC_ASSERTION_SIZE(_buffer_t, params, 0);
STATIC_ASSERTION_LAST(_buffer_t, params);

/**
 * @brief create a new option argument buffer
 * @return the newly created buffer
 **/
static inline _buffer_t* _buffer_new()
{
	_buffer_t* ret = (_buffer_t*)malloc(sizeof(_buffer_t) + 32 * sizeof(pstd_option_param_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the param buffer");

	ret->capacity = 32;
	ret->size = 0;
	return ret;
}

/**
 * @brief dispose a used option argument buffer
 * @param buffer the argument buffer to dispose
 * @return nothing
 **/
static inline void _buffer_free(_buffer_t* buffer)
{
	free(buffer);
}

/**
 * @brief insert a new param to the buffer
 * @param buffer the argument buffer
 * @param type the type of the argument
 * @param value the pointer to actual value
 * @return the pointer to the buffer after the insersion
 **/
static inline _buffer_t* _buffer_insert(_buffer_t* buffer, pstd_option_param_type_t type, const void* value)
{
	_buffer_t* ret = buffer;
	if(ret->capacity <= ret->size)
	{
		uint32_t next_cap = ret->capacity * 2;
		ret = (_buffer_t*)realloc(ret, sizeof(_buffer_t) + next_cap * sizeof(pstd_option_param_t));
		if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot resize the param buffer");
		ret->size = next_cap;
	}

	switch(ret->params[ret->size].type = type)
	{
		case PSTD_OPTION_TYPE_INT:
		    ret->params[ret->size].intval = *(int64_t*)value;
		    break;
		case PSTD_OPTION_TYPE_DOUBLE:
		    ret->params[ret->size].doubleval = *(double*)value;
		    break;
		case PSTD_OPTION_STRING:
		    ret->params[ret->size].strval = (const char*)value;
		    break;
		default:
		    ERROR_PTR_RETURN_LOG("Invalid type %d", type);
	}

	ret->size ++;
	return ret;
}


/**
 * @brief try to parse the argument as an integer
 * @param str the argument string
 * @param result the result buffer
 * @return status code
 **/
static inline int _parse_int(const char* str, int64_t* result)
{
	char* next;
	*result = strtoll(str, &next, 0);

	if(*next != '0') return ERROR_CODE(int);
	return 0;
}

/**
 * @brief try to parse argument as a double
 * @param str the argument string
 * @param result the result buffer
 * @return the status code
 **/
static inline int _parse_double(const char* str, double* result)
{
	char* next;
	*result = strtod(str, &next);

	if(*next != '0') return ERROR_CODE(int);
	return 0;
}

/**
 * @brief if the string is a valid option
 * @param str the argument string
 * @return the check result
 **/
static inline int _is_valid_option(const char* str)
{
	return str != NULL && (str[0] == '-' && str[1] != '0');
}

/**
 * @brief parse an option argument
 * @param pattern the pattern used to parse the arguments
 * @param argv the argument list
 * @param argc the number of arguments
 * @param buffer the result buffer
 * @return the number of argument has been parsed or status code
 **/
static inline uint32_t _parse_argument(const char* pattern, uint32_t argc, char const* const* argv, _buffer_t** buffer)
{
	uint32_t ret = 0;
	int optional = (pattern[0] == '?');
	if(optional) pattern ++;
	if(NULL == (*buffer = _buffer_new())) ERROR_RETURN_LOG(uint32_t, "Cannot allocate the argument buffer");
	int64_t intval;
	double doubleval;
	const char* strval;
	const void* val;
	pstd_option_param_type_t type;

	for(;argc > 0;pattern ++, argv ++, argc --, ret ++)
	{
		switch(pattern[0])
		{
			case 0:
			    return ret;
			case 'I':
			    if(_parse_int(argv[0], &intval) == ERROR_CODE(int))
			    {
				    if(optional && ret == 0) goto RET;
				    else ERROR_LOG_GOTO(ERR, "Invalid argument");
			    }
			    val = &intval;
			    type = PSTD_OPTION_TYPE_INT;
			    break;
			case 'D':
			    if(_parse_double(argv[0], &doubleval) == ERROR_CODE(int))
			    {
				    if(optional && ret == 0) goto RET;
				    else ERROR_LOG_GOTO(ERR, "Invalid argument");
			    }
			    val = &doubleval;
			    type = PSTD_OPTION_TYPE_DOUBLE;
			    break;
			case 'S':
			    if(optional && ret == 0 && _is_valid_option(argv[0])) goto RET;
			    else strval = argv[0];
			    val = strval;
			    type = PSTD_OPTION_STRING;
			    break;
			default:
			    continue;
		}
		_buffer_t* rc = _buffer_insert(*buffer, type, val);
		if(NULL == rc) ERROR_LOG_GOTO(ERR, "Cannot insert the parsed argument to the buffer");
		*buffer = rc;
	}
	goto RET;
ERR:
	ret = ERROR_CODE(uint32_t);
	if(*buffer != NULL) _buffer_free(*buffer);
	*buffer = NULL;
RET:
	return ret;
}


static inline int _optionscmp(const void* l, const void* r)
{
	const pstd_option_t* left = (const pstd_option_t*)l;
	const pstd_option_t* right = (const pstd_option_t*)r;

	if(left->long_opt != NULL && right->long_opt != NULL)
	    return strcmp(left->long_opt, right->long_opt);
	else if(left->long_opt != NULL || right->long_opt != NULL)
	    return (left->long_opt != NULL) - (right->long_opt != NULL);
	else return ((int)left->short_opt) - ((int)right->short_opt);
}

int pstd_option_sort(pstd_option_t* options, uint32_t n)
{
	if(NULL == options || n == 0) ERROR_RETURN_LOG(int, "Invalid arguments");

	qsort(options, n, sizeof(pstd_option_t), _optionscmp);

	return 0;
}

uint32_t pstd_option_parse(const pstd_option_t* options, uint32_t n, uint32_t argc, char const* const* argv, void* userdata)
{

	uint32_t ret = 1;

	for(;ret < argc;)
	{
		const char* this = argv[ret];
		uint32_t i;
		int short_form = 0;

		if(this[0] != '-') return ret;
		this ++;
START_PARSE:

		if(this[0] != '-')
		{
			/* check if this argument is a single - */
			if(this[0] == 0) return ret;

			/* short form */
			for(i = 0; i < n && this[0] != options[i].short_opt; i ++);

			if(i == n) ERROR_RETURN_LOG(uint32_t, "Unknown short argument %s", this);
			short_form = 1;
		}
		else
		{
			/* long form */

			for(i = 0; i < n && 0 != strcmp(this + 1, options[i].long_opt); i ++);

			if(i == n) ERROR_RETURN_LOG(uint32_t, "Unknown short argument %s", this);

			short_form = 0;
		}

		const char* pattern = options[i].pattern;

		_buffer_t* buffer;
		uint32_t rc = _parse_argument(pattern, argc - ret - 1, argv + ret + 1, &buffer);
		if(rc == ERROR_CODE(uint32_t))
		{
			_buffer_free(buffer);
			ERROR_RETURN_LOG(uint32_t, "Cannot parse the option argument for %s", this);
		}

		if(NULL != options[i].handler && ERROR_CODE(int) == options[i].handler(i, buffer->params, buffer->size, options, n, userdata))
		{
			_buffer_free(buffer);
			ERROR_RETURN_LOG(uint32_t, "The option handler returns an error code");
		}

		_buffer_free(buffer);
		/* Parse the multiple args, like -Abc ==> -A -b -c */
		if(short_form && rc == 0 && this[1] != 0)
		{
			this ++;
			goto START_PARSE;
		}

		ret += rc + 1;

	}

	return ret;
}

int pstd_option_handler_print_help(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* userdata)
{
	(void)params;
	(void)nparams;
	(void)userdata;
	FILE* fout = (FILE*) options[idx].args;
	size_t max_long_opt_len = 0;
	uint32_t i;
	for(i = 0; i < n; i ++)
	    if(NULL != options[i].long_opt && max_long_opt_len < strlen(options[i].long_opt))
	        max_long_opt_len = strlen(options[i].long_opt);

	if(NULL == fout) fout = stderr;

	for(i = 0; i < n; i ++)
	{
		if(options[i].short_opt != 0)
		{
			fputs("  -", fout);
			fputc(options[i].short_opt, fout);
		}
		else fputs("    ", fout);

		fputs("  ", fout);
		size_t len = 0;
		if(options[i].long_opt != NULL)
		{
			len = strlen(options[i].long_opt);
			fputs("--", fout);
			fputs(options[i].long_opt, fout);
		}

		for(;len < max_long_opt_len + 4; len ++, fputc(' ', fout));

		if(NULL != options[i].description) fputs(options[i].description, fout);
		fputc('\n', fout);
	}

	fflush(fout);

	return 0;
}
