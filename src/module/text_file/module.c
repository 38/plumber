/**
 * Copyright (C) 2018, Hao Hou
 **/

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/mman.h>

#include <itc/module_types.h>

#include <error.h>

#include <utils/log.h>

#include <module/text_file/module.h>

/**
 * @brief The module context
 * @note Since we can rely on the framework to make sure all the poped up lines regions are properly disposed.
 *       So the mapped address in here, only records the next page that is going to be used next time. 
 *       Thus the same thing for the in_mapped_size.
 **/
typedef struct {
	char*  label;          /*!< The label for this module */

	char*  in_file_path;   /*!< The path to the input file */
	char   in_line_delim;  /*!< The line delimitor for the input file */

	char*  out_file_path;  /*!< The path to the output file */
	int    out_file_flag;  /*!< The flag for the output file (O_CREAT, O_TRUNC, O_WRONLY, etc) */
	int    out_file_perm;  /*!< The permission code if we need to create a new output */
	uint32_t create_only:1;/*!< If we only allow newly created output */

	int    in_fd;          /*!< The input file descriptor */
	int    out_fd;         /*!< The output file descriptor */

	void*  in_mapped;      /*!< The base address for the input file has been mapped to the memory */
	size_t in_mapped_size; /*!< The size of the memory region (Not page aligned yet) */
} _context_t;

/**
 * @brief Indicates one or serveral continous page that has been used togther
 **/
typedef struct {
	void*    start_addr;   /*!< The start point of the memory region */
	uint32_t n_pages;      /*!< The number of pages for this region */
	uint32_t refcnt;       /*!< The referecen counter for this region */
} _mapped_region_t;

/**
 * @brief Describes a single line 
 **/
typedef struct {
	const char*       line;       /*!< The line text */
	size_t            size;       /*!< The size of the line (Not including the deliminator)*/
	_mapped_region_t* regions[2]; /*!< Since we only can have two regions at most */
} _line_t;

/**
 * @brief The data strcture to describe the handle
 **/
typedef struct {
	uint32_t  is_in:1; /*!< If this is the input side of the IO event */
	_line_t*  line;    /*!< The actual line data for this IO event */
	size_t    offset;  /*!< The offset for the read side */
} _handle_t;

static size_t _pagesize = 0;

static int _init(void* __restrict ctxmem, uint32_t argc, char const* __restrict const* __restrict argv)
{
	if(0 == _pagesize)
		_pagesize = (size_t)getpagesize();

	_context_t* ctx = (_context_t*)ctxmem;

	memset(ctx, 0, sizeof(_context_t));

	static const char* param_name[] = {"input=", "output=", "label="};
	char* arguments[sizeof(param_name) / sizeof(param_name[0])] = {};

	uint32_t i, j, k;

	for(i = 0; i < argc; i ++)
	{
		for(j = 0; j < sizeof(param_name) / sizeof(param_name[0]); j ++)
		{
			for(k = 0; param_name[j][k] && param_name[j][k] == argv[i][k]; k ++);
			if(param_name[j][k] == 0) break;
		}
		if(j < sizeof(param_name) / sizeof(param_name[0]))
		{
			if(arguments[j] != NULL)
				ERROR_LOG_GOTO(ERR, "Duplicated module init parameter: %s", argv[i]);
			if(NULL == (arguments[j] = strdup(argv[i] + k)))
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the parameter string");
		}
		else ERROR_LOG_GOTO(ERR, "Invalid module initialization param: %s. Expected "
				                 "text_file input=<input> output=<output> label=<label>", argv[i]);
	}

#define _ASSIGN_PARAM(idx, var, desc) do {\
	if(NULL == (ctx->var = arguments[idx]))\
		ERROR_LOG_GOTO(ERR, "Missing "desc". Expected text_file input=<input> output=<output> label=<label>");\
} while(0)
	_ASSIGN_PARAM(0, in_file_path, "input file");
	_ASSIGN_PARAM(1, out_file_path, "output file");
	_ASSIGN_PARAM(2, label, "label name");

	ctx->in_line_delim = '\n';
	ctx->out_file_perm = 0644;
	ctx->out_file_flag = O_WRONLY | O_CREAT | O_TRUNC;


	return 0;
ERR:
	for(i = 0; i < sizeof(arguments) / sizeof(arguments[0]); i ++)
		if(NULL != arguments[i]) free(arguments[i]);

	return ERROR_CODE(int);
}

static int _cleanup(void* __restrict ctxmem)
{
	int rc = 0;
	_context_t* ctx = (_context_t*)ctxmem;

	if(NULL != ctx->in_file_path) 
		free(ctx->in_file_path);

	if(NULL != ctx->out_file_path)
		free(ctx->out_file_path);

	if(NULL != ctx->label)
		free(ctx->label);

	if(ctx->in_fd > 0 && close(ctx->in_fd) < 0)
		rc = ERROR_CODE(int);

	if(ctx->out_fd > 0 && close(ctx->out_fd) < 0)
		rc = ERROR_CODE(int);

	if(NULL != ctx->in_mapped)
	{
		if(munmap(ctx->in_mapped, ((ctx->in_mapped_size + _pagesize - 1) / _pagesize) * _pagesize) < 0)
			rc = ERROR_CODE(int);
	}

	return rc;
}

static const char* _get_path(void* __restrict ctxmem, char* buf, size_t sz)
{
	_context_t* ctx = (_context_t*)ctxmem;
	snprintf(buf, sz, "%s", ctx->label);
	return buf;
}

static itc_module_property_value_t _make_str(const char* str)
{
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_STRING,
		.str = strdup(str)
	};

	if(NULL == ret.str)
	{
		ret.type = ITC_MODULE_PROPERTY_TYPE_ERROR;
		LOG_ERROR_ERRNO("Cannot allocate memory for the string result");
	}
	return ret;
}

static itc_module_property_value_t _make_int(int64_t val)
{
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_INT,
		.num  = val
	};

	return ret;
}

static itc_module_property_value_t _get_prop(void* __restrict ctxmem, const char* sym)
{
	_context_t* ctx = (_context_t*)ctxmem;
	if(strcmp(sym, "input") == 0)  return _make_str(ctx->in_file_path);
	if(strcmp(sym, "output") == 0) return _make_str(ctx->out_file_path);
	if(strcmp(sym, "output_perm") == 0) return _make_int(ctx->out_file_perm);
	if(strcmp(sym, "output_mode") == 0)
	{
		if(ctx->create_only)
			return _make_str("create");
		switch(ctx->out_file_flag)
		{
			case O_WRONLY | O_CREAT | O_TRUNC:
				return _make_str("override");
			case O_WRONLY | O_CREAT | O_APPEND:
				return _make_str("append");
			default:
				return _make_str("invalid mode");
		}
	}
	if(strcmp(sym, "delimitor") == 0)
	{
		char buf[2] = {ctx->in_line_delim, 0};
		return _make_str(buf);
	}

	itc_module_property_value_t ret = {.type = ITC_MODULE_PROPERTY_TYPE_NONE};

	return ret;
}

static int _set_prop(void* __restrict ctxmem, const char* __restrict sym, itc_module_property_value_t val)
{
	_context_t* ctx = (_context_t*)ctxmem;
	if(val.type == ITC_MODULE_PROPERTY_TYPE_INT)
	{
		if(strcmp(sym, "output_perm") == 0)
		{
			ctx->out_file_perm = (int)val.num;
			return 1;
		}
	}
	else if(val.type == ITC_MODULE_PROPERTY_TYPE_STRING)
	{
		if(strcmp(sym, "output_mode") == 0)
		{
			ctx->create_only = 0;
			if(strcmp(val.str, "create") == 0)
				ctx->create_only = 1;
			else if(strcmp(val.str, "override") == 0)
				ctx->out_file_flag = O_WRONLY | O_CREAT | O_TRUNC;
			else if(strcmp(val.str, "append") == 0)
				ctx->out_file_flag = O_WRONLY | O_CREAT | O_APPEND;
			else ERROR_RETURN_LOG(int, "Invalid type string, acceptable type: create, override, append");
			return 1;
		}

		if(strcmp(sym, "delimitor") == 0)
		{
			ctx->in_line_delim = val.str[0];
			return 1;
		}

	}

	return 0;
}

static itc_module_flags_t _get_flags(void* __restrict ctx)
{
	_context_t* context = (_context_t*)ctx;
	return ITC_MODULE_FLAGS_EVENT_LOOP | (context->in_mapped == NULL ? ITC_MODULE_FLAGS_EVENT_EXHUASTED : 0);
}

itc_module_t module_text_file_module_def = {
	.mod_prefix     = "pipe.text_file",
	.handle_size    = sizeof(_handle_t),
	.context_size   = sizeof(_context_t),
	.module_init    = _init,
	.module_cleanup = _cleanup,
	.get_path       = _get_path,
	.get_property   = _get_prop,
	.set_property   = _set_prop,
	.get_flags      = _get_flags
};

