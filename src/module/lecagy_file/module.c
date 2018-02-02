/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include<itc/module_types.h>
#include <module/legacy_file/module.h>
#include <utils/log.h>

#include <error.h>
/** @brief the pipe handle */
typedef struct _handle_t {
	int    is_input; /*!< indicate if this is a input pipe */
	FILE*  fp;       /*!< the file pointer */
	struct _handle_t* next_fork; /*!< the next fork of this handle */
} _handle_t;

static inline int _init(void* __restrict ctx, uint32_t argc, char const* __restrict const* __restrict argv)
{
	(void) ctx;
	(void) argc;
	(void) argv;
	return 0;
}
static inline int _cleanup(void* __restrict ctx)
{
	(void) ctx;
	return 0;
}
/**
 * @note this function should get only either memory for the input side or the output side.
 *       And the another side of the pipe should be a file on disk.
 **/
static inline int _allocate(void* __restrict ctx, uint32_t hint, void* __restrict o_mem, void* __restrict i_mem, const void* __restrict args)
{
	(void) ctx;
	(void) hint;

	if(NULL == args || (i_mem == NULL && o_mem == NULL) || (i_mem != NULL && o_mem != NULL))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_handle_t* pipe = NULL;
	const char* mode = NULL;
	int is_input;

	if(NULL != i_mem)
	{
		is_input = 1;
		pipe = (_handle_t*)i_mem;
		mode = "r";
	}
	if(NULL != o_mem)
	{
		is_input = 0;
		pipe = (_handle_t*)o_mem;
		mode = "w";
	}

	pipe->fp = fopen((const char*)args, mode);
	if(NULL == pipe->fp) ERROR_RETURN_LOG_ERRNO(int, "Cannot open the file %s", (const char*)args);

	pipe->is_input = is_input;
	pipe->next_fork = NULL;

	return 0;
}


static size_t _read(void * __restrict ctx, void* __restrict buffer, size_t nbytes, void* __restrict h)
{
	(void) ctx;

	if(NULL == buffer || NULL == h) ERROR_RETURN_LOG(size_t, "Invalid arguments");

	_handle_t* pipe = (_handle_t*)h;

	size_t rc = fread(buffer, 1, nbytes, pipe->fp);

	for(pipe = pipe->next_fork; NULL != pipe; pipe = pipe->next_fork)
	    fwrite(buffer, 1, rc, pipe->fp);

	return rc;
}

static int _deallocate(void * __restrict ctx, void* __restrict h, int error, int purge)
{
	(void) ctx;
	(void) error;
	(void) purge;
	if(NULL == h) ERROR_RETURN_LOG(int, "Invalid arguments");

	_handle_t* pipe = (_handle_t*)h;

	char buffer[128];

	if(pipe->is_input) while(_read(ctx, buffer, sizeof(buffer), pipe));

	fclose(pipe->fp);
	LOG_DEBUG("File pipe has been disposed");

	return 0;
}

static size_t _write(void * __restrict ctx, const void* __restrict buffer, size_t nbytes, void* __restrict h)
{
	(void) ctx;
	if(NULL == buffer || NULL == h) ERROR_RETURN_LOG(size_t, "Invalid arguments");
	_handle_t* pipe = (_handle_t*)h;

	size_t rc = fwrite(buffer, 1, nbytes, pipe->fp);

	for(pipe = pipe->next_fork; NULL != pipe; pipe = pipe->next_fork)
	    fwrite(buffer, 1, rc, pipe->fp);

	return rc;

}

static int _has_unread(void* __restrict ctx, void* __restrict pipe)
{
	(void)ctx;
	if(NULL == pipe) ERROR_RETURN_LOG(int, "Invalid arguments");

	_handle_t* h = (_handle_t*)pipe;

	if(!h->is_input) ERROR_RETURN_LOG(int, "Call has_unread_data function on a output pipe");

	int rc = feof(h->fp);
	if(rc < 0) ERROR_RETURN_LOG_ERRNO(int, "Cannot call feof");

	return !rc;
}

static int _fork(void* __restrict ctx, void* __restrict dest, void* __restrict src, const void* __restrict args)
{
	(void) ctx;
	const char* filename = (const char*)args;
	_handle_t* sh = (_handle_t*)src;
	_handle_t* dh = (_handle_t*)dest;
	FILE* fp = fopen(filename, "w");
	if(fp == NULL) ERROR_RETURN_LOG_ERRNO(int, "Cannot open the file");
	dh->is_input = 0;
	dh->fp = fp;

	dh->next_fork = sh->next_fork;
	sh->next_fork = dh;

	return 0;
}

static const char* _get_path(void* __restrict ctx, char* buf, size_t sz)
{
	(void) ctx;
	(void) sz;
	buf[0] = 0;
	return buf;
}

itc_module_t module_legacy_file_module_def = {
	.handle_size = sizeof(_handle_t),
	.context_size = 0,
	.module_init = _init,
	.mod_prefix = "pipe.legacy_file",
	.module_cleanup = _cleanup,
	.allocate = _allocate,
	.deallocate = _deallocate,
	.read = _read,
	.write = _write,
	.fork = _fork,
	.has_unread_data = _has_unread,
	.get_path = _get_path
};
