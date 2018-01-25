/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <error.h>

#include <utils/log.h>

#include <itc/module_types.h>
#include <module/file/module.h>

typedef enum {
	_WRITE_MODE_OVERRIDE,/*!< We just want to override the existing file. If it's not existing, create a new one */
	_WRITE_MODE_APPEND,  /*!< We just want to append to the existing file. If it's not existing, create a new one */
	_WRITE_MODE_CREATE   /*!< This only allows the file name that currently not exist, otherwise an error will be raised */
} _write_mode_t;

typedef enum {
	_READ_MODE_LINE,          /*!< A line based text file */
	_READ_MODE_FS_BLK,        /*!< A fixed sized block */
	_READ_MODE_VS_BLK         /*!< A variant size block (TODO: how to determine the size? since different file might do this differently)*/
} _read_mode_t;

/**
 * @brief The module isntance context
 * @details parallel_num: For a line based text file, we don't know where the next line begins
 *                        unless we see the line deliminator. Thus this limit us unable to do
 *                        real parallel IO (although parallel processing is still possible).
 *                        The solution to this problem is look at the file in multiple locations
 *                        and search for the begining for the line. In this way, we are able to
 *                        read different lines in parallel. The number is the limit for how many
 *                        locations we need to look at.
 * @todo Currently, what we have for the output file is "out of order" IO, which means we don't have
 *       the guareentee that the output line will have the same order as the input file. But it seems
 *       we do need this some times. So how we support that ?
 **/
typedef struct {
	/************** Input related field **************/
	_read_mode_t  read_mode;    /*!< What mode we are using */
	uint32_t      parallel_num; /*!< How many parallel reading can happen at the same time. (See struct doc for details) */
	char*         in_path;      /*!< The input path */
	int           in_fd;        /*!< The input file file descriptor */
	void*         in_mem;       /*!< The input memoroy only used when the mmap mode is enabled */

	/************** Output related fields ************/
	char*         out_path;     /*!< The output path */
	int           out_fd;       /*!< The output file descriptor */
	char*         label;        /*!< The label for this file */
	_write_mode_t write_mode;   /*!< Which mode we want to use for the output file */
	int           create_mode;  /*!< The mode for newly created file */

	/************** Module state *********************/
	uint32_t  exhuasted:1;  /*!< Indicates if this module have no events */

	union {
		struct {
			char          delim;        /*!< The line deliminator char */
		}                 line_param;   /*!< The line based IO param */
		struct {
			size_t        size;         /*!< The size for the fix-sized block */
		}                 fs_blk_param; /*!< The fixed sized block IO param */
	};
} context_t;

/**
 * @brief The pipe handle data structure
 **/
typedef struct {
	void*   buf;       /*!< The current line buffer */
	size_t  buf_size;  /*!< The size of this line buffer */
	size_t  offset;    /*!< Current read offset */
} handle_t;

static int _init(void* __restrict ctxbuf, uint32_t argc, char const* __restrict const* __restrict argv)
{
	context_t* ctx = (context_t*)ctxbuf;
	
	memset(ctx, 0, sizeof(context_t));
	ctx->in_fd = ctx->out_fd = -1;
	ctx->line_param.delim = '\n';

	const char* arguments[3]  = {};

	static const char* names[] = {"input=", "output=", "label="};

	uint32_t i,j,k;
	for(i = 0; i < argc; i ++)
	{
		for(j = 0; j < sizeof(names) / sizeof(names[1]); j ++)
		{
			for(k = 0; names[j][k] && names[j][k] == argv[i][k]; k ++);
			if(names[j][k] == 0) break;
		}

		if(j < sizeof(arguments) / sizeof(arguments[0]))
			arguments[j] = argv[i] + k;
		else
			ERROR_RETURN_LOG(int, "Invalid module init string: %s, expected: file input=<input-file> output=<output-file> label=<label>", argv[i]);
	}

	for(i = 0; i < sizeof(arguments) / sizeof(arguments[0]); i ++)
		if(arguments[i] == NULL)
			ERROR_RETURN_LOG(int, "Missing argument string %s, expected: file input=<input-file> output=<output-file> label=<label>", names[i]);
		else 
			LOG_DEBUG("Parsed argument string %s%s", names[i], arguments[i]);

	if(NULL == (ctx->in_path = strdup(arguments[0])))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the input path");

	if(NULL == (ctx->out_path = strdup(arguments[1])))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dpilicate the output path");

	if(NULL == (ctx->label = strdup(arguments[2])))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the label name");

	return 0;
}

static inline int _cleanup(void* __restrict ctxbuf)
{
	int rc = 0;
	context_t* ctx = (context_t*)ctxbuf;

	if(NULL != ctx->in_path) free(ctx->in_path);

	if(NULL != ctx->out_path) free(ctx->out_path);

	if(NULL != ctx->label) free(ctx->label);

	if(ctx->in_fd >= 0 && close(ctx->in_fd) < 0)
	{
		LOG_ERROR_ERRNO("Cannot close the input file");
		rc = ERROR_CODE(int);
	}

	if(ctx->out_fd >= 0 && close(ctx->out_fd) < 0)
	{
		LOG_ERROR_ERRNO("Cannot close the output file");
		rc = ERROR_CODE(int);
	}

	return rc;
}

static const char* _get_path(void* __restrict ctxbuf, char* buffer, size_t size)
{
	context_t* ctx = (context_t*)ctxbuf;
	snprintf(buffer, size, "%s", ctx->label);

	return buffer;
}

static itc_module_flags_t _get_flags(void* __restrict ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	return ITC_MODULE_FLAGS_EVENT_LOOP | (ctx->exhuasted ? ITC_MODULE_FLAGS_EVENT_EXHUASTED : 0);
}

static inline itc_module_property_value_t _make_str(const char* str)
{
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_STRING,
		.str  = strdup(str)
	};

	if(ret.str != NULL) return ret;

	LOG_ERROR_ERRNO("Cannot duplicate the result string");

	ret.type = ITC_MODULE_PROPERTY_TYPE_ERROR;
	return ret;
}

static inline itc_module_property_value_t _make_int(int64_t val)
{
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_INT,
		.num  = val
	};
	return ret;
}

static const char* _read_mode_name[] = {
	[_READ_MODE_LINE]     "line",
	[_READ_MODE_FS_BLK]   "fixed-size",
	[_READ_MODE_VS_BLK]   "variant-size"
};

static const char* _write_mode_name[] = {
	[_WRITE_MODE_CREATE]   "create",
	[_WRITE_MODE_OVERRIDE] "override",
	[_WRITE_MODE_APPEND]   "append"
};

static itc_module_property_value_t _get_prop(void* __restrict ctxmem, const char* sym)
{
	context_t* ctx = (context_t*)ctxmem;
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_NONE
	};


	if(strcmp(sym, "input_path") == 0) return _make_str(ctx->in_path);
	else if(strcmp(sym, "output_path") == 0) return _make_str(ctx->out_path);
	else if(strcmp(sym, "write_mode") == 0) return _make_str(_write_mode_name[ctx->write_mode]);
	else if(strcmp(sym, "read_mode") == 0) return _make_str(_read_mode_name[ctx->read_mode]);
	else if(ctx->read_mode == _READ_MODE_LINE && strcmp(sym, "line.delim") == 0) 
	{
		char buf[] = {ctx->line_param.delim, 0};
		return _make_str(buf);
	}
	else if(ctx->read_mode == _READ_MODE_FS_BLK && strcmp(sym, "fs_blk.size") == 0)
		return _make_int((int64_t)ctx->fs_blk_param.size);
	else if(strcmp(sym, "parallel") == 0) return _make_int(ctx->parallel_num);
	else if(strcmp(sym, "output_perm") == 0) return _make_int(ctx->create_mode);

	return ret;
}

static inline uint32_t _parse_mode_str(char const* const* arr, size_t n, const char* str)
{
	uint32_t i;
	for(i = 0; i < n && strcmp(arr[i], str); i ++);

	if(i >= n)
	{
		LOG_ERROR("Invalid mode string: %s", str);
		LOG_INFO("Possible mode:");
		for(i = 0; i < n; i ++)
			LOG_INFO("\t%s", arr[i]);
		return ERROR_CODE(uint32_t);
	}

	return i;
}

static int _set_prop(void* __restrict ctxmem, const char* sym, itc_module_property_value_t value)
{
	context_t* ctx = (context_t*)ctxmem;

	if(value.type == ITC_MODULE_PROPERTY_TYPE_STRING)
	{
		if(strcmp(sym, "write_mode") == 0)
		{
			uint32_t option = _parse_mode_str(_write_mode_name, sizeof(_write_mode_name)/sizeof(_write_mode_name[0]), value.str);
			if(ERROR_CODE(uint32_t) == option) return 0;
			ctx->write_mode = (_write_mode_t)option;
			return 1;
		}
		else if(strcmp(sym, "read_mode") == 0)
		{
			uint32_t option = _parse_mode_str(_read_mode_name, sizeof(_read_mode_name)/sizeof(_read_mode_name[0]), value.str);
			if(ERROR_CODE(uint32_t) == option) return 0;
			switch(ctx->read_mode = (_read_mode_t)option)
			{
				case _READ_MODE_LINE:
					ctx->line_param.delim = '\n';
					break;
				case _READ_MODE_FS_BLK:
					ctx->fs_blk_param.size = 32;
					break;
				case _READ_MODE_VS_BLK:
					LOG_WARNING("Fixme: support variant length block");
			}
			
			return 1;
		}
		else if(ctx->read_mode == _READ_MODE_LINE && strcmp(sym, "line.delim") == 0)
		{
			if(value.str[0] != 0 && value.str[1] != 0)
				LOG_WARNING("Ignoring the extra charecter in the string");

			ctx->line_param.delim = value.str[0];
			return 1;
		}
	}
	else if(value.type == ITC_MODULE_PROPERTY_TYPE_INT)
	{
		if(strcmp(sym, "parallel") == 0) ctx->parallel_num = (uint32_t)value.num;
		else if(strcmp(sym, "output_perm") == 0) ctx->create_mode = (int)value.num;
		else if(ctx->read_mode == _READ_MODE_FS_BLK && strcmp(sym, "fs_blk.size") == 0)
			ctx->fs_blk_param.size = (size_t)value.num;
		else  return 0;
		return 1;
	}

	return 0;
}

itc_module_t module_file_module_def = {
	.context_size   = sizeof(context_t),
	.handle_size    = sizeof(handle_t),
	.mod_prefix     = "pipe.file",
	.module_init    = _init,
	.module_cleanup = _cleanup,
	.get_path       = _get_path,
	.get_flags      = _get_flags,
	.get_property   = _get_prop,
	.set_property   = _set_prop
};
