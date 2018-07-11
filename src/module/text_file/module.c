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
#include <signal.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/select.h>

#include <itc/module_types.h>

#include <error.h>

#include <utils/log.h>
#include <utils/thread.h>

#include <module/text_file/module.h>

/**
 * @brief Indicates one or serveral continous page that has been used togther
 **/
typedef struct {
	void*    start_addr;   /*!< The start point of the memory region */
	uint32_t n_pages;      /*!< The number of pages for this region */
	uint16_t refcnt;       /*!< The referecen counter for this region */
} _mapped_region_t;

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

	uint32_t is_init:1;    /*!< Indicates if we have fully intialized the servlet */

	uint32_t use_mmap:1;   /*!< If we should use mmap */

	int    in_fd;          /*!< The input file descriptor */
	int    out_fd;         /*!< The output file descriptor */

	union {
		struct {
			void*  in_mapped;      /*!< The base address for the input file has been mapped to the memory */
			void*  in_mapped_end;  /*!< The end point for the mapped memory */
			size_t in_mapped_size; /*!< The size of the memory region (Not page aligned yet) */

			_mapped_region_t* last_region; /*!< The last region we have read */
			char*             unread;      /*!< The start point of the unread memory */

			pthread_mutex_t   write_mutex; /*!< The mutex used for write */
			thread_pset_t*    lw_buf;      /*!< The local write buffer */
		} mmap;                            /*!< The mmap based context */

		struct {
			volatile uint32_t is_eof:1;          /*!< If we have reached the EOF */
			volatile uint32_t is_eol:1;          /*!< If we have reached the EOL */
			volatile uint32_t is_released:1;     /*!< If the lock has been released */
			pthread_mutex_t  io_mutex;          /*!< We only allow 1 event poped out each time */
			pthread_cond_t   io_cond;           /*!< The IO condition variable */
		} fd;                                   /*!< The FD based context */
	};

} _context_t;

typedef struct {
	size_t capacity;   /*!< The capacity of this buffer */
	size_t used;       /*!< The used size of this buffer */
	uintpad_t __padding__[0];
	char   buffer[0];     /*!< The actual buffer address */
} _local_write_buf_t;

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
	_line_t   line;    /*!< The actual line data for this IO event */
	size_t    offset;  /*!< The offset for the read side */
} _handle_t;

static size_t _pagesize = 0;

static void* _lw_buf_alloc(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;

	_local_write_buf_t* ret = (_local_write_buf_t*)malloc(sizeof(_local_write_buf_t) + 4096);
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the local buffer");

	ret->capacity = 4096;
	ret->used = 0;

	return ret;
}

static int _lw_buf_dealloc(void* mem, const void* data)
{
	int rc = 0;
	const _context_t* ctx = (const _context_t*)data;
	_local_write_buf_t* buf = (_local_write_buf_t*)mem;

	if(ctx->out_fd > 0)
	{
		size_t start = 0;
		while(buf->used > start)
		{
			ssize_t write_rc = write(ctx->out_fd, buf->buffer + start, buf->used - start);
			if(write_rc < 0)
			{
				LOG_ERROR_ERRNO("Cannot write the data to the target file");
				rc = ERROR_CODE(int);
				break;
			}
			start += (size_t) write_rc;
		}
	}

	free(buf);

	return rc;
}

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

	ctx->fd.is_eof = 0;

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

	if(ctx->use_mmap && NULL != ctx->mmap.lw_buf && ERROR_CODE(int) == thread_pset_free(ctx->mmap.lw_buf))
	    rc = ERROR_CODE(int);

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

	if(ctx->use_mmap)
	{
		if((void*)-1 != ctx->mmap.in_mapped && NULL != ctx->mmap.in_mapped)
		{
			if(munmap(ctx->mmap.in_mapped, ((ctx->mmap.in_mapped_size + _pagesize - 1) / _pagesize) * _pagesize) < 0)
				rc = ERROR_CODE(int);
		}

		if(0 != (errno = pthread_mutex_destroy(&ctx->mmap.write_mutex)))
			rc = ERROR_CODE(int);
	}
	else
	{
		if(0 != (errno = pthread_mutex_destroy(&ctx->fd.io_mutex)))
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
	if(context->use_mmap)
	{
		return ITC_MODULE_FLAGS_EVENT_LOOP |
			   (context->mmap.in_mapped == NULL &&
				context->mmap.last_region == NULL &&
				context->is_init ? ITC_MODULE_FLAGS_EVENT_EXHUASTED : 0);
	}
	else
	{
		return ITC_MODULE_FLAGS_EVENT_LOOP | 
			   (context->fd.is_eof ? ITC_MODULE_FLAGS_EVENT_EXHUASTED : 0);
	}
}

static inline int _ensure_init(_context_t* ctx)
{
	if(ctx->is_init) return 0;
	
	ctx->is_init = 1;

	if((ctx->in_fd = open(ctx->in_file_path, O_RDONLY)) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot open the input file");

	if(ctx->create_only && access(ctx->out_file_path, F_OK) == 0)
	    ERROR_RETURN_LOG(int, "Output file %s already exists", ctx->out_file_path);

	if(ctx->create_only && errno != ENOENT)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot access the output file");

	if((ctx->out_fd = open(ctx->out_file_path, ctx->out_file_flag, ctx->out_file_perm)) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot open the output file");

	if(ctx->use_mmap)
	{
		if(0 != (errno = pthread_mutex_init(&ctx->mmap.write_mutex, NULL)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot create the write mutex");
		
		if(NULL == (ctx->mmap.lw_buf = thread_pset_new(32, _lw_buf_alloc, _lw_buf_dealloc, ctx)))
			ERROR_RETURN_LOG(int, "Cannot create the thread local for the local write buffer");

		struct stat buf;

		if(fstat(ctx->in_fd, &buf) < 0)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot access the metadata of the input file");

		ctx->mmap.in_mapped_size = (size_t)buf.st_size;

		if((void*)-1 == (ctx->mmap.in_mapped = mmap(NULL, ((ctx->mmap.in_mapped_size + _pagesize - 1) / _pagesize) * _pagesize,
										  PROT_READ, MAP_PRIVATE, ctx->in_fd, 0)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot map the file to address");

		LOG_INFO("Mapped address [%p, %p)", ctx->mmap.in_mapped, (char*)ctx->mmap.in_mapped + ctx->mmap.in_mapped_size);

		ctx->mmap.unread = ctx->mmap.in_mapped;
		ctx->mmap.in_mapped_end = (char*)ctx->mmap.in_mapped + ctx->mmap.in_mapped_size;
	}
	else
	{
		if(0 != (errno = pthread_mutex_init(&ctx->fd.io_mutex, NULL)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot create the IO mutex for text file");

		if(0 != (errno = pthread_cond_init(&ctx->fd.io_cond, NULL)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot create the cond var for the text file");

		int orignal_fl = fcntl(ctx->in_fd, F_GETFL, NULL);

		if(orignal_fl < 0)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot get the orignal file flag");

		if(fcntl(ctx->in_fd, F_SETFL, orignal_fl | O_NONBLOCK) < 0)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot set the input file to non-blocking mode");

		ctx->fd.is_released = 1;
	}

	return 0;
}

static inline void _incref_region(_mapped_region_t* region)
{
	uint16_t old;
	do{
		old = region->refcnt;
	}while(!__sync_bool_compare_and_swap(&region->refcnt, old, old + 1));
}

static inline void _decref_region(_mapped_region_t* region)
{
	if(region->refcnt == 0)
	    LOG_ERROR("Code bug: decref a zero referenced region");

	uint16_t old;
	do {
		old = region->refcnt;
	} while(!__sync_bool_compare_and_swap(&region->refcnt, old, old - 1));

	if(old == 1)
	{
		if(munmap(region->start_addr, _pagesize * region->n_pages) < 0)
		    LOG_WARNING_ERRNO("Cannot unmap the mapped region [%p, %p)",
		                       region->start_addr,
		                       (char*)region->start_addr + _pagesize * region->n_pages);
		else
		    LOG_INFO("Mapped memory page [%p, %p) has been ummaped",
		             region->start_addr,
		             (char*)region->start_addr + _pagesize * region->n_pages);
		free(region);
	}
}

static inline int _region_new(_context_t* ctx, char* begin, size_t size)
{
	_mapped_region_t* prev_region = ctx->mmap.last_region;

	/* TODO: use the memory pool */
	if(NULL == (ctx->mmap.last_region = (_mapped_region_t*)malloc(sizeof(_mapped_region_t))))
	    ERROR_RETURN_LOG(int, "Cannot allocate memory for the region object");
	ctx->mmap.last_region->start_addr = (void*)begin;
	ctx->mmap.last_region->n_pages = (uint32_t)((size + _pagesize - 1) / _pagesize);
	/* By default our module holds a reference to the last region */
	ctx->mmap.last_region->refcnt = 1;
	ctx->mmap.in_mapped = (void*)(begin + ctx->mmap.last_region->n_pages * _pagesize);
	if(ctx->mmap.last_region->n_pages * _pagesize >= ctx->mmap.in_mapped_size)
	{
		/* If this is the last page */
		ctx->mmap.in_mapped_size = 0;
		ctx->mmap.in_mapped = NULL;
	}
	else ctx->mmap.in_mapped_size -= ctx->mmap.last_region->n_pages * _pagesize;

	if(NULL != prev_region) _decref_region(prev_region);

	return 0;
}

static inline int _ensure_region(_context_t* ctx, _mapped_region_t** region1, _mapped_region_t** region2, char* begin, char* end)
{
	/* If we don't have the last region, we need to create a new region */
	if(NULL == ctx->mmap.last_region && ERROR_CODE(int) ==  _region_new(ctx, begin, (size_t)(end - begin)))
	    ERROR_RETURN_LOG(int, "Cannot allocate memory for the next region object");

	_incref_region(ctx->mmap.last_region);
	*region1 = ctx->mmap.last_region;

	char* region_end = (char*)ctx->mmap.last_region->start_addr + _pagesize * ctx->mmap.last_region->n_pages;

	/* If the memory region is outside of the last region, we need to make a new one */
	if(region_end < end)
	{
		if(ERROR_CODE(int) == _region_new(ctx, region_end, (size_t)(end - region_end)))
		    ERROR_RETURN_LOG(int, "Cannot create a new region");
		_incref_region(ctx->mmap.last_region);
		*region2 = ctx->mmap.last_region;
		region_end = (char*)ctx->mmap.last_region->start_addr + _pagesize * ctx->mmap.last_region->n_pages;
	}
	else *region2 = NULL;

	/* If the region is used up by current line, just dereference the current region */
	if(region_end == end || end == ctx->mmap.in_mapped_end)
	{
		_decref_region(ctx->mmap.last_region);
		ctx->mmap.last_region = NULL;
	}

	return 0;
}

static inline int _wait_for_input_ready(const _context_t* ctx)
{
	fd_set set;
	struct timeval timeout = {
		.tv_usec = 1000
	};

	FD_ZERO(&set);
	FD_SET(ctx->in_fd, &set);

	int src = select(ctx->in_fd + 1, &set, NULL, NULL, &timeout);
	if(src < 0)
	{
		if(errno != EINTR)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot wait for the FD ready");
		else
			return 0;
	}
	else if(src == 0)
		return 0;

	return 1;
}

static int _accept(void* __restrict ctxmem, const void* __restrict args, void* __restrict inmem, void* __restrict outmem)
{
	(void)args;

	_context_t* ctx = (_context_t*)ctxmem;

	if(ERROR_CODE(int) == _ensure_init(ctx))
	    ERROR_RETURN_LOG(int, "Cannot initialize the context");

	_handle_t* in = (_handle_t*)inmem;
	_handle_t* out = (_handle_t*)outmem;

	in->is_in = 1;
	out->is_in = 0;

	if(ctx->use_mmap)
	{
		char* begin = ctx->mmap.unread;
		char* end   = memchr(ctx->mmap.unread, ctx->in_line_delim, (size_t)((char*)ctx->mmap.in_mapped_end - begin));

		if(NULL == end) end = ctx->mmap.unread + ctx->mmap.in_mapped_size;
		else end ++;

		if(end - begin == 0)
		{
			LOG_NOTICE("End of file reached, terminating the event loop");
			return ERROR_CODE(int);
		}

		if(ERROR_CODE(int) == _ensure_region(ctx, in->line.regions, in->line.regions + 1, begin, end))
			ERROR_RETURN_LOG(int, "Cannot make region for next line");

		ctx->mmap.unread = end;

		in->line.line = begin;
		in->line.size = (size_t)(end - begin);
		in->offset = 0;

		out->offset = 0;
		out->line = in->line;
	}
	else
	{

		if(0 != (errno = pthread_mutex_lock(&ctx->fd.io_mutex)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot lock the IO mutex");

		while(0 == ctx->fd.is_released)
		{
			if(0 != (errno = pthread_cond_wait(&ctx->fd.io_cond, &ctx->fd.io_mutex)))
				ERROR_RETURN_LOG_ERRNO(int, "Cannot wait for the IO cond var");
		}
		
		ctx->fd.is_released = 0;

		if(0 != (errno = pthread_mutex_unlock(&ctx->fd.io_mutex)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot unlock the IO mutex");

		if(ctx->fd.is_eof) 
		{
			LOG_NOTICE("Input file %s reached EOF, exiting the event loop", ctx->in_file_path);
			return ERROR_CODE(int);
		}

		for(;;)
		{
			int wrc = _wait_for_input_ready(ctx);
			if(wrc == ERROR_CODE(int))
				ERROR_RETURN_LOG(int, "Cannot wait for the input FD gets ready");
			
			if(wrc == 0 && ctx->fd.is_eof)
			{
				LOG_NOTICE("End of file reached, terminating the event loop");
				return ERROR_CODE(int);
			}

			if(wrc > 0) break;
		}

		ctx->fd.is_eof = 0;
		ctx->fd.is_eol = 0;
	}

	return 0;
}

static int _dealloc(void* __restrict ctxmem, void* __restrict pipe, int error, int purge)
{
	(void)error;
	_context_t* ctx = (_context_t*)ctxmem;
	_handle_t* handle = (_handle_t*)pipe;
	if(ctx->use_mmap)
	{
		if(NULL == pipe || handle->line.regions[0] == NULL)
			ERROR_RETURN_LOG(int, "Invalid arguments");

		if(purge)
		{
			uint32_t i;
			for(i = 0; i < 2; i ++)
			{
				if(handle->line.regions[i] != NULL)
					_decref_region(handle->line.regions[i]);
			}
		}
	}
	else
	{
		if(purge)
		{
			if(0 != (errno = pthread_mutex_lock(&ctx->fd.io_mutex)))
				ERROR_RETURN_LOG_ERRNO(int, "Cannot lock the IO mutex");

			ctx->fd.is_released = 1;

			if(0 != (errno = pthread_cond_signal(&ctx->fd.io_cond)))
				ERROR_RETURN_LOG_ERRNO(int, "Cannot signal the IO cond var");

			if(0 != (errno = pthread_mutex_unlock(&ctx->fd.io_mutex)))
				ERROR_RETURN_LOG_ERRNO(int, "Cannot unlock the IO mutex");
		}
	}

	return 0;
}

static int _fork(void* __restrict ctxmem, void* __restrict dest, void* __restrict src, const void* __restrict args)
{
	(void) ctxmem;
	(void) args;

	_handle_t* from = (_handle_t*)src;
	_handle_t* to   = (_handle_t*)dest;

	if(!from->is_in)
	    ERROR_RETURN_LOG(int, "Invalid pipe direction");

	to->offset = 0;
	to->line = from->line;
	to->is_in = 1;

	return 0;
}

static size_t _read(void* __restrict ctxmem, void* __restrict buf, size_t n, void* __restrict pipe)
{
	_context_t* ctx = (_context_t*)ctxmem;

	if(ctx->use_mmap)
	{
		_handle_t* handle = (_handle_t*)pipe;

		if(!handle->is_in)
			ERROR_RETURN_LOG(size_t, "Input pipe port expected");

		size_t bytes_to_read = n;
		if(bytes_to_read > handle->line.size - handle->offset)
			bytes_to_read = handle->line.size - handle->offset;

		memcpy(buf, handle->line.line + handle->offset, bytes_to_read);

		handle->offset += bytes_to_read;

		return bytes_to_read;
	}
	else
	{
		size_t rc = 0;
		char *cbuf = (char*)buf;

		if(ctx->fd.is_eol)
			return 0;

		for(;n > 0;rc ++, n --)
		{
			char cur_buf;

			ssize_t sz;
			while((sz = read(ctx->in_fd, &cur_buf, 1)) < 0)
			{
				if(errno != EWOULDBLOCK || errno != EAGAIN)
					ERROR_RETURN_LOG_ERRNO(size_t, "Cannot read");

				int wrc = _wait_for_input_ready(ctx);

				if(wrc == ERROR_CODE(int))
					return ERROR_CODE(size_t);
				else if(wrc == 0 && ctx->fd.is_eof)
					return 0;

			}

			if(sz == 0)
			{
				ctx->fd.is_eof = 1;
				break;
			}

			cbuf[rc] = cur_buf;

			if(cur_buf == ctx->in_line_delim)
			{
				ctx->fd.is_eol = 1;
				rc ++;
				break;
			}
		}

		return rc;
	}
}

static inline int _write_fd(_context_t* ctx, const void* __restrict buf, size_t n)
{
	int rc = 0;

	if((errno = pthread_mutex_lock(&ctx->mmap.write_mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot lock the write mutex");

	if(write(ctx->out_fd, buf, n) < 0)
	{
		rc = ERROR_CODE(int);
		LOG_ERROR_ERRNO("Cannot write data to the output file");
	}

	if((errno = pthread_mutex_unlock(&ctx->mmap.write_mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot release the write mutex");

	return rc;
}

static size_t _write(void* __restrict ctxmem, const void* __restrict data, size_t n, void* __restrict pipe)
{
	(void)pipe;
	_context_t* ctx = (_context_t*)ctxmem;

	if(ctx->use_mmap)
	{
		_local_write_buf_t* buf = (_local_write_buf_t*)thread_pset_acquire(ctx->mmap.lw_buf);

		if(NULL == buf) ERROR_RETURN_LOG(size_t, "Cannot get the thread local buffer");

		size_t buf_size = buf->capacity - buf->used;

		if(buf_size < n)
		{
			if(_write_fd(ctx, buf->buffer, buf->used) == ERROR_CODE(int))
				ERROR_RETURN_LOG(size_t, "Cannot write the bufferred data to the file");

			buf->used = 0;
		}

		if(buf->capacity < n)
		{
			LOG_DEBUG("The buffer is smaller than the data to write");

			if(ERROR_CODE(int) == _write_fd(ctx, data, n))
				ERROR_RETURN_LOG(size_t, "Cannot write the required data to the file");
		}
		else
		{
			memcpy(buf->buffer + buf->used, data, n);
			buf->used += n;
		}

		return n;
	}
	else
	{
		ssize_t write_rc = write(ctx->out_fd, data, n); 

		if(write_rc < 0)
		{
			if(errno == EWOULDBLOCK || errno == EAGAIN)
				return 0;
			ERROR_RETURN_LOG_ERRNO(size_t, "Cannot write data to the output fd");
		}

		return (size_t)write_rc;
	}
}

static int _has_unread(void* __restrict ctxmem, void* __restrict pipe)
{
	_context_t* ctx = (_context_t*)ctxmem;

	if(ctx->use_mmap)
	{

		_handle_t* handle = (_handle_t*)pipe;

		if(!handle->is_in)
			ERROR_RETURN_LOG(int, "Input pipe port expected");

		return handle->offset < handle->line.size;
	}

	return !ctx->fd.is_eof && !ctx->fd.is_eol;

}

static int _get_internal_buf(void* __restrict ctxmem, void const** __restrict result, size_t* __restrict min_size, size_t* __restrict max_size, void* __restrict pipe)
{
	_context_t* ctx = (_context_t*)ctxmem;
	if(NULL == result || NULL == min_size || NULL == max_size)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ctx->use_mmap)
	{
		_handle_t* handle = (_handle_t*)pipe;

		size_t bytes_to_read = *max_size;

		if(bytes_to_read > handle->line.size - handle->offset)
			bytes_to_read = handle->line.size - handle->offset;

		if(bytes_to_read < *min_size)
		{
			*max_size = *min_size = 0;
			*result = NULL;
			return 0;
		}

		*result = handle->line.line + handle->offset;

		*max_size = *min_size = bytes_to_read;

		handle->offset += bytes_to_read;

		return 1;
	}
	else
	{
		*max_size = *min_size = 0;
		*result = NULL;
		return 0;
	}
}

static int _release_internal_buf(void* __restrict context, void const* __restrict buffer, size_t actual_size, void* __restrict handle)
{
	(void)context;
	(void)buffer;
	(void)actual_size;
	(void)handle;
	return 0;
}

static void _event_loop_killed(void* __restrict ctx)
{
	_context_t* context = (_context_t*)ctx;

	if(!context->use_mmap)
	{
		context->fd.is_eof = 1;
		context->fd.is_released = 1;
	}
}


itc_module_t module_text_file_module_def = {
	.mod_prefix      = "pipe.text_file",
	.handle_size     = sizeof(_handle_t),
	.context_size    = sizeof(_context_t),
	.module_init     = _init,
	.module_cleanup  = _cleanup,
	.get_path        = _get_path,
	.get_property    = _get_prop,
	.set_property    = _set_prop,
	.get_flags       = _get_flags,
	.accept          = _accept,
	.deallocate      = _dealloc,
	.fork            = _fork,
	.read            = _read,
	.write           = _write,
	.has_unread_data = _has_unread,
	.get_internal_buf = _get_internal_buf,
	.release_internal_buf = _release_internal_buf,
	.event_thread_killed = _event_loop_killed
};

