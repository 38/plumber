/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <itc/module_types.h>
#include <module/test/module.h>

#include <utils/log.h>
#include <utils/static_assertion.h>
#include <utils/mempool/objpool.h>
#include <utils/mempool/page.h>

#include <error.h>

/**
 * @brief the enum indicates the type of the handle
 **/
typedef enum {
	_INPUT,    /*!< a input handle, comsumer */
	_OUTPUT    /*!< a output handle, producer */
} _type_t;

/**
 * @brief the struct used to represents a data page
 **/
typedef struct _buffer_page_t {
	struct _buffer_page_t* next;  /*!< the next page in the mem buffer */
	uint32_t size;                /*!< the actual data size in this page */
	uintpad_t __padding__[0];
	char   data[0];               /*!< the data section */
} _buffer_page_t;
STATIC_ASSERTION_LAST(_buffer_page_t, data);
STATIC_ASSERTION_SIZE(_buffer_page_t, data, 0);

/**
 * @brief the actual data definition for a module handle
 **/
typedef struct {
	_type_t type;                   /*!< the type of the handle */
	uint32_t page_offset;           /*!< current offset in the buffer page, used as read pointer */
	_buffer_page_t* current_page;   /*!< the current page location, used as read pointer */
	_buffer_page_t* buffer;         /*!< the actual buffer page list */
} module_handle_t;

/**
 * @brief the size of the page
 **/
static uint32_t _pagesize;

/**
 * @brief the bytes of a data page can hold
 **/
static uint32_t _pagedata_limit;

static _buffer_page_t* __buffer_page_new(void)
{
	_buffer_page_t* ret = (_buffer_page_t*)mempool_page_alloc();
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new page");
	ret->next = NULL;
	ret->size = 0;
	return ret;
}
static inline int __buffer_free(_buffer_page_t* page)
{
	int rc = 0;
	for(;NULL != page;)
	{
		_buffer_page_t* tmp = page;
		page = page->next;
		if(ERROR_CODE(int) == mempool_page_dealloc(tmp))
			rc = ERROR_CODE(int);
	}
	return rc;
}

static int _module_init(void* __restrict ctx, uint32_t argc, char const* __restrict const* __restrict argv)
{
	(void) ctx;
	(void) argc;
	(void) argv;

	int rc = getpagesize();
	if(rc < 0) ERROR_RETURN_LOG_ERRNO(int, "Cannot get the page size");
	else LOG_DEBUG("The page size is %d", rc);
	_pagesize = (uint32_t)rc;
	_pagedata_limit = _pagesize - (uint32_t)sizeof(_buffer_page_t);
	return 0;
}

static int _module_cleanup(void* __restrict ctx)
{
	(void) ctx;
	return 0;
}

static int _allocate(void* __restrict ctx, uint32_t hint, void* __restrict out, void* __restrict in, const void* __restrict args)
{
	(void)ctx;
	(void)args;
	(void)hint;

	module_handle_t* input = (module_handle_t*)in;
	module_handle_t* output = (module_handle_t*)out;

	input->type = _INPUT;
	output->type = _OUTPUT;

	if(NULL == (input->current_page = output->current_page = output->buffer = input->buffer = __buffer_page_new()))
		ERROR_RETURN_LOG(int, "Cannot allocate buffer for the mempipie");

	input->page_offset = output->page_offset = 0;
	LOG_DEBUG("pipe has been created!");
	return 0;
}

static int _deallocate(void* __restrict ctx, void* __restrict pipe, int error, int purge)
{
	(void) ctx;
	(void) error;
	module_handle_t* handle = (module_handle_t*)pipe;

	if(purge)
	{
		LOG_DEBUG("pipe has been disposed");
		return __buffer_free(handle->buffer);
	}

	LOG_DEBUG("one side of the pipe dead");
	return 0;
}

static int _get_internal_buf(void* __restrict ctx, void const** __restrict result, size_t* __restrict min_size, size_t* __restrict max_size, void* __restrict pipe)
{
	(void) ctx;
	if(NULL == result || NULL == min_size || NULL == max_size)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	module_handle_t* handle = (module_handle_t*)pipe;
	uint32_t actual_size;

	if(handle->type != _INPUT)
		ERROR_RETURN_LOG(int, "Invalid type of pipe, a output pipe cannot be read");


	if(handle->current_page != NULL)
	{
		 actual_size = handle->current_page->size - handle->page_offset;

		 if(actual_size == 0 && handle->current_page->next != NULL)
		 {
			 handle->page_offset = 0;
			 handle->current_page = handle->current_page->next;
			 actual_size = handle->current_page->size;
		 }

		if(actual_size < *min_size) goto RET_EMPTY;

		if(actual_size < *max_size) *max_size = actual_size;
	}
	else
	{
RET_EMPTY:
		LOG_DEBUG("The size limit cannot satisfied, returning empty");
		*max_size = *min_size = 0;
		result = NULL;
		return 0;
	}

	*result = handle->current_page->data + handle->page_offset;

	*min_size = *max_size;

	return 1;
}

static int _release_internal_buf(void* __restrict context, void const* __restrict buffer, size_t actual_size, void* __restrict handle)
{
	/* Since in any case we are returning determined-length memory regions, so just do nothing here */
	(void)context;
	(void)buffer;
	(void)actual_size;
	(void)handle;
	return 0;
}

static size_t _read(void* __restrict ctx, void* __restrict buffer, size_t nbytes, void* __restrict pipe)
{
	(void) ctx;
	module_handle_t* handle = (module_handle_t*)pipe;

	if(handle->type != _INPUT)
		ERROR_RETURN_LOG(size_t, "Invalid type of pipe, a output pipe cannot be read");

	size_t ret = 0;
	char * b = (char*)buffer;
	for(;handle->current_page != NULL && nbytes > 0; handle->current_page = handle->current_page->next, handle->page_offset = 0)
	{
		uint32_t size = handle->current_page->size - handle->page_offset;

		if(size > nbytes) size = (uint32_t)nbytes;

		memcpy(b, handle->current_page->data + handle->page_offset, size);

		b += size;
		nbytes -= size;
		ret += size;
		handle->page_offset += size;
		if(nbytes == 0) break;
	}

	return ret;
}

static size_t _write(void* __restrict ctx, const void* __restrict buffer, size_t nbytes, void* __restrict pipe)
{
	(void) ctx;

	module_handle_t* handle = (module_handle_t*)pipe;

	if(handle->type != _OUTPUT)
		ERROR_RETURN_LOG(size_t, "Invalid type of pipe, a write function cannot take a input pipe");

	const char* b = (const char*)buffer;
	size_t ret = 0;

	for(;nbytes > 0;)
	{
		uint32_t size = _pagedata_limit - handle->page_offset;
		if(nbytes < size) size = (uint32_t)nbytes;
		memcpy(handle->current_page->data + handle->page_offset, b, size);

		handle->page_offset += size;
		handle->current_page->size += size;
		b += size;
		nbytes -= size;
		ret += size;
		if(handle->current_page->size == _pagedata_limit)
		{
			if(NULL != handle->current_page->next)
				ERROR_RETURN_LOG(size_t, "Unexpected current page in a write pipe, code bug!");
			if(NULL == (handle->current_page->next = __buffer_page_new()))
				ERROR_RETURN_LOG(size_t, "Cannot create new page for the mempipe");
			handle->page_offset = 0;
			handle->current_page = handle->current_page->next;
		}
	}

	return ret;
}

static int _fork(void* __restrict ctx, void* __restrict dest, void* __restrict src, const void* __restrict args)
{
	(void) ctx;
	(void) args;
	module_handle_t* dh = (module_handle_t*)dest;
	module_handle_t* sh = (module_handle_t*)src;

	dh->type = _INPUT;
	dh->page_offset = 0;
	dh->current_page = dh->buffer = sh->buffer;

	return 0;
}

static int _has_unread_data(void* __restrict ctx, void* __restrict data)
{
	(void) ctx;
	module_handle_t* handle = (module_handle_t*) data;
	if(NULL == handle)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(handle->type != _INPUT)
		ERROR_RETURN_LOG(int, "Cannot perform has_unread call on a output pipe");

	for(;handle->current_page != NULL && handle->current_page->size <= handle->page_offset;)
	{
		handle->current_page = handle->current_page->next;
		handle->page_offset = 0;
	}

	return handle->current_page != NULL;
}

static const char* _get_path(void* __restrict ctx, char* buf, size_t sz)
{
	(void) ctx;
	(void) sz;
	buf[0] = 0;
	return buf;
}

itc_module_t module_mem_module_def = {
	.handle_size = sizeof(module_handle_t),
	.mod_prefix = "pipe.mem",
	.context_size = 0,
	.module_init = _module_init,
	.module_cleanup = _module_cleanup,
	.allocate = _allocate,
	.deallocate = _deallocate,
	.read = _read,
	.write = _write,
	.fork = _fork,
	.has_unread_data = _has_unread_data,
	.get_path = _get_path,
	.get_internal_buf = _get_internal_buf,
	.release_internal_buf = _release_internal_buf
};
