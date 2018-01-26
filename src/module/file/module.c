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
#include <sys/mman.h>

#include <error.h>

#include <utils/log.h>

#include <itc/module_types.h>
#include <module/file/module.h>

typedef enum {
	_WRITE_MODE_OVERRIDE,/*!< We just want to override the existing file. If it's not existing, create a new one */
	_WRITE_MODE_APPEND,  /*!< We just want to append to the existing file. If it's not existing, create a new one */
	_WRITE_MODE_CREATE   /*!< This only allows the file name that currently not exist, otherwise an error will be raised */
} _write_mode_t;

/**
 * @todo Support the variant-sized block file, but we need to desgin a way to describe the VS_BLK structure. 
 *       Another thing is the VS_BLK mode shouldnt support multi-region reading since it can not detect the 
 *       record begining 
 **/
typedef enum {
	_READ_MODE_LINE,          /*!< A line based text file */
	_READ_MODE_FS_BLK,        /*!< A fixed sized block */
	_READ_MODE_VS_BLK         /*!< A variant size block (TODO: how to determine the size? since different file might do this differently)*/
} _read_mode_t;

/**
 * @brief an mapped memory region
 **/
typedef struct {
	uint32_t  inuse:1;   /*!< Indicates if current region is using by application layer code */
	uint32_t  last:1;    /*!< Indicates this is the last region of the file */
	char*     current;   /*!< The current read pointer */
	char*     begin;     /*!< The beging of the region */
	char*     end;       /*!< The end of the region */
} _mem_region_t;

/**
 * @brief Describe a input file
 **/
typedef struct {
	char*          path;      /*!< The input path */
	int            fd;        /*!< The input file file descriptor */
	size_t         size;      /*!< The size of the input file */
	_mem_region_t* regions;   /*!< Each of the access regions */
} _input_file_t;

/**
 * @brief Indicates that a parallel read sequence that we can read currently
 **/
typedef struct {
	uint32_t is_mmap_region:1;   /*!< Indicates if this is a mmaped region */
	union {
		_input_file_t* file;     /*!< The file that doesn't support file */
		_mem_region_t* region;   /*!< The mmaped region that can be read */
	};
} _input_sequence_t;

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

	_read_mode_t  read_mode;    /*!< What mode we are using */
	int           create_mode;  /*!< The mode for newly created file */

	/************** Input related field **************/
	uint32_t       parallel_num; /*!< How many parallel reading can happen at the same time (For a single file). (See struct doc for details) */
	uint32_t       num_inputs;   /*!< The number of input file */
	_input_file_t* inputs;       /*!< The input file list */

	/************** Output related fields ************/
	char*         out_path;     /*!< The output path */
	int           out_fd;       /*!< The output file descriptor */
	char*         label;        /*!< The label for this file */
	_write_mode_t write_mode;   /*!< Which mode we want to use for the output file */

	/************** Module state *********************/
	uint32_t  input_init:1; /*!< Indicates all the inputs has initialized */
	uint32_t  exhuasted:1;  /*!< Indicates if this module have no events */

	_input_sequence_t* avail_list; /*!< The avaiable list */
	uint32_t avail_list_begin;     /*!< The avaiable list begin */
	uint32_t avail_list_end;       /*!< The aviable list end */
	uint32_t avail_list_size;      /*!< The size of the availe list */


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
	uint32_t  is_input:1;/*!< If this handle is the input end of the pipe */
	void*     buf;       /*!< The current line buffer */
	size_t    buf_size;  /*!< The size of this line buffer (-1 for the buffer we don't know the size) */
	size_t    offset;    /*!< Current read offset */
} handle_t;

static size_t _pagesize = 0;

#define _ROUND_PAGE(sz) ((sz + _pagesize - 1) & ~(_pagesize - 1))

static int _init(void* __restrict ctxbuf, uint32_t argc, char const* __restrict const* __restrict argv)
{
	if(_pagesize == 0)
		_pagesize = (size_t)getpagesize();

	context_t* ctx = (context_t*)ctxbuf;
	
	memset(ctx, 0, sizeof(context_t));
	ctx->out_fd = -1;
	ctx->line_param.delim = '\n';
	ctx->parallel_num = 1;

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
	j = 0;
	for(i = 0;; i ++)
	{
		if((arguments[0][i] == ',' || arguments[0][i] == 0) && i > j)
		{
			_input_file_t* tmp = realloc(ctx->inputs, (ctx->num_inputs + 1) * sizeof(_input_file_t));
			if(NULL == tmp) 
				ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the input array");
			tmp[ctx->num_inputs].fd = -1;
			tmp[ctx->num_inputs].regions = NULL;
			tmp[ctx->num_inputs].size = 0;

			ctx->inputs = tmp;

			if(NULL == (tmp[ctx->num_inputs].path = malloc(i - j + 1)))
				ERROR_RETURN_LOG_ERRNO(int, "Cannot allocae memory for the path");

			memcpy(tmp[ctx->num_inputs].path, arguments[0] + j, i - j);
			ctx->num_inputs ++;
			j = i + 1;
		}

		if(arguments[0][i] == 0) break;
	}

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

	if(NULL != ctx->out_path) free(ctx->out_path);

	if(NULL != ctx->label) free(ctx->label);

	if(ctx->inputs != NULL)
	{
		uint32_t i;
		for(i = 0; i < ctx->num_inputs; i ++)
		{
			if(ctx->inputs[i].fd >= 0 && close(ctx->inputs[i].fd) < 0)
			{
				LOG_ERROR_ERRNO("Cannot close the input file %u", i);
				rc = ERROR_CODE(int);
			}

			if(NULL != ctx->inputs[i].path) 
				free(ctx->inputs[i].path);

			if(NULL != ctx->inputs[i].regions)
			{
				uint32_t j;
				for(j = 0; j < ctx->parallel_num; j ++)
				{
					if(NULL == ctx->inputs[i].regions[j].begin) 
						continue;

					size_t region_size = _ROUND_PAGE((size_t)((char*)ctx->inputs[i].regions[j].end - (char*)ctx->inputs[i].regions[j].begin)); 

					if(region_size > 0 && munmap(ctx->inputs[i].regions[j].begin, region_size) < 0)
					{
						LOG_ERROR_ERRNO("Cannot unmap the region [%p,%p)", ctx->inputs[i].regions[j].begin, ctx->inputs[i].regions[j].end);
						rc = ERROR_CODE(int);
					}
				}
			}

		}
		free(ctx->inputs);
	}

	if(NULL != ctx->avail_list)
		free(ctx->avail_list);

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


	if(strlen(sym) > 11 && memcmp(sym, "input_path$", 11) == 0) 
	{
		uint32_t num = (uint32_t)atol(sym + 11);
		if(ctx->num_inputs <= num) return ret;
		return _make_str(ctx->inputs[num].path);
	}
	else if(strcmp(sym, "input_count") == 0)
	{
		return _make_int(ctx->num_inputs);
	}
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
		if(strcmp(sym, "parallel") == 0 && (uint32_t)value.num > 0) ctx->parallel_num = (uint32_t)value.num;
		else if(strcmp(sym, "output_perm") == 0) ctx->create_mode = (int)value.num;
		else if(ctx->read_mode == _READ_MODE_FS_BLK && strcmp(sym, "fs_blk.size") == 0)
			ctx->fs_blk_param.size = (size_t)value.num;
		else  return 0;
		return 1;
	}

	return 0;
}

static int _ensure_inputs(context_t* ctx)
{
	if(ctx->input_init) return 0;

	size_t max_num_seq = ctx->num_inputs * ctx->parallel_num;
	
	ctx->avail_list_size = 1;
	for(;ctx->avail_list_size < max_num_seq; ctx->avail_list_size *= 2);

	if(NULL == (ctx->avail_list = (_input_sequence_t*)malloc(sizeof(_input_sequence_t) * ctx->avail_list_size)))
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the avaiable list");

	ctx->avail_list_begin = ctx->avail_list_end = 0;

	uint32_t i,j;
	for(i = 0; i < ctx->num_inputs; i ++)
	{
		if((ctx->inputs[i].fd = open(ctx->inputs[i].path, O_RDONLY)) < 0)
		{
			LOG_WARNING_ERRNO("Unable to open file %s", ctx->inputs[i].path);
			continue;
		}

		struct stat stbuf;
		if(fstat(ctx->inputs[i].fd, &stbuf) < 0)
		{
			LOG_NOTICE_ERRNO("Unable to stat the file to get the file size, do not use mmap IO");
			goto STREAM_IO;
		}

		size_t region_size = _ROUND_PAGE((size_t)stbuf.st_size);

		ctx->inputs[i].size = (size_t)stbuf.st_size;

		if(NULL == (ctx->inputs[i].regions = (_mem_region_t*)calloc(ctx->parallel_num, sizeof(_mem_region_t))))
		{
			LOG_WARNING_ERRNO("Cannot allocate memory for the region");
			goto STREAM_IO;
		}

		if(region_size > 0)
		{
			char* region = (char*)mmap(NULL, region_size, PROT_READ, MAP_PRIVATE, ctx->inputs[i].fd, 0);
			if(NULL == region)
			{
				LOG_NOTICE_ERRNO("Unable to mmap the file into address space, try to use normal IO");
				goto STREAM_IO;
			}
			else LOG_INFO("Mapped memory region: %s => [%p, %p)", ctx->inputs[i].path, region, region + ctx->inputs[i].size);

			size_t block_size = (ctx->inputs[i].size + ctx->parallel_num - 1) / ctx->parallel_num;
			size_t block_offset = 0;

			for(j = 0; j < ctx->parallel_num; j ++)
			{
				if(block_offset >= ctx->inputs[i].size) 
				{
					ctx->inputs[i].regions[j].begin = ctx->inputs[i].regions[j].end = NULL;
					continue;
				}

				size_t desired_end = block_offset + block_size;
				size_t actual_end = _ROUND_PAGE(desired_end);

				if(actual_end > ctx->inputs[i].size) 
				{
					actual_end = ctx->inputs[i].size;
					ctx->inputs[i].regions[j].last = 1;
				}

				ctx->inputs[i].regions[j].begin = region + block_offset;
				ctx->inputs[i].regions[j].end   = region + actual_end;
				ctx->inputs[i].regions[j].current = begin;

				if(ctx->read_mode == _READ_MODE_LINE)
				{
					/* In this case, we should make current location after the first '\n' */

					if(j > 0 && ctx->inputs[i].regions[j - 1].end[-1] != ctx->eol_marker)
					{
						char* next = (char*)memchr(ctx->inputs[i].regions[j].begin, (char)ctx->eol_marker, ctx->inputs[i].regions[j].end - ctx->inputs[i].regions[j].begin);
						if(NULL == next)
						{
							/* This indicates that the entire region belongs to previous line */
						}
						else ctx->inputs[i].regions[j].current = next + 1;
					}
				}

				LOG_INFO("Memory map region %u: [%p, %p) file = %s", j, ctx->inputs[i].regions[j].begin, ctx->inputs[i].regions[j].end, ctx->inputs[i].path);

				block_offset = actual_end;

				ctx->avail_list[ctx->avail_list_end].is_mmap_region = 1;
				ctx->avail_list[ctx->avail_list_end].region = &ctx->inputs[i].regions[j];
				ctx->avail_list_end ++;
			}
		}
		continue;
STREAM_IO:
		ctx->avail_list[ctx->avail_list_end].is_mmap_region = 0;
		ctx->avail_list[ctx->avail_list_end].file = &ctx->inputs[i];
		ctx->avail_list_end ++;
	}

	return 0;
}

static int _accept(void* __restrict ctxmem, const void* __restrict args, void* __restrict inbuf, void* __restrict outbuf)
{
	(void)args;
	context_t* ctx = (context_t*)ctxmem;

	if(ERROR_CODE(int) == _ensure_inputs(ctx))
		ERROR_RETURN_LOG(int, "Cannot ensure all the input file are initialized");

	handle_t* in = (handle_t*)inbuf;
	handle_t* out = (handle_t*)outbuf;

	in->is_input = 1;
	in->buf = NULL;
	in->buf_size = (size_t)-1;
	in->offset = 0;

	out->is_input = 0;

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
	.set_property   = _set_prop,
	.accept         = _accept
};
