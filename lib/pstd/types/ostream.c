/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <utils/static_assertion.h>

#include <pstd/types/ostream.h>
#include <pstd/scope.h>
#include <pstd/mempool.h>

/**
 * @brief Indicates which kinds of block we are in
 **/
typedef enum {
	_BLOCK_TYPE_PAGE,      /*!< a data page */
	_BLOCK_TYPE_MEMORY,    /*!< a data buffer that passed from the outside */
	_BLOCK_TYPE_STREAM     /*!< another RLS stream */ 
} _block_type_t;

/**
 * @brief The actual page section for a data page
 **/
typedef struct {
	uint32_t   size;          /*!< The actual amount of data we have in the page */
	uint32_t   read;          /*!< The read pointer */
	uintpad_t  __padding__[0];
	char       data[0];       /*!< The actual data section */
} _page_data_t;
STATIC_ASSERTION_LAST(_page_data_t, data);
STATIC_ASSERTION_SIZE(_page_data_t, data, 0);

/**
 * @brief The actual data for the memory buffer block
 **/
typedef struct {
	int (*free_func)(void*);    /*!< The free function for this memory block */
	size_t size;                /*!< The size of the block */
	size_t read;                /*!< The read pointer */
	void*  data;                /*!< The actual data section */
} _memory_buf_t;

/**
 * @brief The RLS stream 
 **/
typedef struct {
	pstd_scope_stream_t*  stream;  /*!< The actual RLS stream */
} _rls_stream_t;

/**
 * @brief The actual data type for a block
 **/
typedef struct _block_t {
	_block_type_t     type;         /*!< The type of current block */
	struct _block_t*  next;         /*!< The next block */
	uintpad_t __padding__[0];
	_page_data_t      page[0];      /*!< The pointer to the page data */
	_memory_buf_t     memory[0];    /*!< The pointer to the memory buffer data */
	_rls_stream_t     stream[0];    /*!< The pointer to the stream data */
} _block_t;
STATIC_ASSERTION_LAST(_block_t, page);
STATIC_ASSERTION_LAST(_block_t, memory);
STATIC_ASSERTION_LAST(_block_t, stream);

/**
 * @brief The actual output stream 
 **/
struct _pstd_ostream_t {
	uint32_t    commited:1;    /*!< If this object has been committed */
	uint32_t    opened:1;      /*!< If this object has been opened previously */
	_block_t*   list_begin;    /*!< The block list begin */
	_block_t*   list_end;      /*!< The block list end */
};

static size_t pagesize = 0;

static inline size_t _page_block_bytes_availiable(const _block_t* page_block)
{
	return pagesize - page_block->page->size - sizeof(_block_t) - sizeof(_page_data_t);
}

static inline _block_t* _page_block_new(void)
{
	_block_t* ret = pstd_mempool_page_alloc();

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate a new data page");

	ret->type = _BLOCK_TYPE_PAGE;
	ret->next = NULL;
	ret->page->size = 0;
	ret->page->read = 0;

	return ret;
}

static inline int _page_block_free(_block_t* page_block)
{
	return pstd_mempool_page_dealloc(page_block);
}

static inline _block_t* _memory_block_new(void* mem, size_t size, int (*free_func)(void*))
{
	_block_t* ret = pstd_mempool_alloc(sizeof(_block_t) + sizeof(_memory_buf_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate a new memory data page");

	ret->type = _BLOCK_TYPE_MEMORY;
	ret->next = NULL;
	ret->memory->size = size;
	ret->memory->read = 0;
	ret->memory->data = mem;
	ret->memory->free_func = free_func;

	return ret;
}

static inline int _memory_block_free(_block_t* memory_block)
{
	int ret = 0;

	if(NULL != memory_block->memory->free_func && ERROR_CODE(int) == memory_block->memory->free_func(memory_block->memory->data))
		ret = ERROR_CODE(int);

	if(ERROR_CODE(int) == pstd_mempool_free(memory_block))
		ret = ERROR_CODE(int);

	return ret;
}

static inline _block_t* _stream_block_new(scope_token_t token)
{
	_block_t* ret = pstd_mempool_alloc(sizeof(_block_t) + sizeof(_rls_stream_t));
	if(NULL == ret) 
		ERROR_PTR_RETURN_LOG("Cannot allocate a new stream block object");

	pstd_scope_stream_t* stream = pstd_scope_stream_open(token);

	if(NULL == stream)
		ERROR_LOG_GOTO(ERR, "Cannot open the RLS token %u as stream", token);

	ret->type = _BLOCK_TYPE_STREAM;
	ret->next = NULL;
	ret->stream->stream = stream;

	return ret;
ERR:
	pstd_mempool_free(ret);
	return NULL;
}

static inline int _stream_block_free(_block_t* stream_block)
{
	int ret = 0;
	if(ERROR_CODE(int) == pstd_scope_stream_close(stream_block->stream->stream))
		ret = ERROR_CODE(int);

	if(ERROR_CODE(int) == pstd_mempool_free(stream_block))
		ret = ERROR_CODE(int);

	return ret;
}

static inline int _block_free(_block_t* block)
{
	switch(block->type)
	{
		case _BLOCK_TYPE_PAGE:
			return _page_block_free(block);
		case _BLOCK_TYPE_MEMORY:
			return _memory_block_free(block);
		case _BLOCK_TYPE_STREAM:
			return _stream_block_free(block);
	}

	return ERROR_CODE(int);
}

static inline int _ostream_free(pstd_ostream_t* ostream, int app_space)
{
	int rc = 0;

	if(app_space && ostream->commited)
		ERROR_RETURN_LOG(int, "Cannot dispose a commited RLS object");

	_block_t* ptr;
	for(ptr = ostream->list_begin; NULL != ptr;)
	{
		_block_t* this = ptr;
		ptr = ptr->next;

		if(ERROR_CODE(int) == _block_free(this))
			rc = ERROR_CODE(int);
	}

	return rc;
}

pstd_ostream_t* pstd_ostream_new(void)
{
	if(pagesize == 0)
		pagesize = (size_t)getpagesize();

	pstd_ostream_t* ret = (pstd_ostream_t*)pstd_mempool_alloc(sizeof(*ret));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the ostream RLS object");

	ret->commited = 0;
	ret->opened = 0;
	ret->list_begin = ret->list_end = NULL;

	return ret;
}

int pstd_ostream_free(pstd_ostream_t* ostream)
{
	return _ostream_free(ostream, 1);
}

int pstd_ostream_write(pstd_ostream_t* stream, const void* buf, size_t sz)
{
	/* Bascially once the ostream is commited, we can't change anything */
	if(NULL == stream || NULL == buf || stream->opened)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	while(sz > 0)
	{
		if(stream->list_end == NULL || 
		   stream->list_end->type != _BLOCK_TYPE_PAGE || 
		   _page_block_bytes_availiable(stream->list_end))
		{
			_block_t* new_block = _page_block_new();
			if(NULL == new_block)
				ERROR_RETURN_LOG(int, "Cannot allocate new block page");
			
			if(stream->list_end != NULL)
				stream->list_end->next = new_block;
			else
				stream->list_begin = new_block;
			stream->list_end = new_block;
		}

		uint32_t bytes_to_write = (uint32_t)_page_block_bytes_availiable(stream->list_end);

		if(bytes_to_write > sz)
			bytes_to_write = (uint32_t)sz;

		memcpy(stream->list_end->page->data + stream->list_end->page->size, buf, bytes_to_write);

		sz -= bytes_to_write;
		buf = ((const char*)buf) + bytes_to_write;
		stream->list_end->page->size += (uint32_t)bytes_to_write;
	}

	return 0;
}

int pstd_ostream_write_owner_pointer(pstd_ostream_t* stream, void* buf, int (*free_func)(void*), size_t sz)
{
	if(NULL == stream || NULL == buf || stream->opened)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(stream->list_end != NULL && stream->list_end->type == _BLOCK_TYPE_PAGE && sz <= _page_block_bytes_availiable(stream->list_end))
	{
		LOG_DEBUG("The last data page is larger than the buffer to write, copy it to the last buffer");
		memcpy(stream->list_end->page->data + stream->list_end->page->size, buf, sz);

		if(NULL != free_func && ERROR_CODE(int) == free_func(buf))
			ERROR_RETURN_LOG(int, "Cannot dispose the used memory buffer");

		return 0;
	}

	_block_t* new_block = _memory_block_new(buf, sz, free_func);
	if(new_block == NULL)
		ERROR_RETURN_LOG(int, "Cannot allocate next memory block");

	if(stream->list_end != NULL)
		stream->list_end->next = new_block;
	else
		stream->list_begin = new_block;
	stream->list_end = new_block;

	return 0;
}

int pstd_ostream_write_scope_token(pstd_ostream_t* stream, scope_token_t token)
{
	if(NULL == stream || 0 == token || ERROR_CODE(scope_token_t) == token || stream->opened)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_block_t* new_block = _stream_block_new(token);

	if(NULL == new_block)
		ERROR_RETURN_LOG(int, "Cannot allocate next stream block");

	if(stream->list_end != NULL)
		stream->list_end->next = new_block;
	else
		stream->list_begin = new_block;
	stream->list_end = new_block;

	return 0;
}

static int _printf_buf_free(void* buf)
{
	free(buf);
	return 0;
}

int pstd_ostream_printf(pstd_ostream_t* stream, const char* fmt, ...)
{
	char _lb[1024];
	size_t _bsz = sizeof(_lb);
	char* _b = _lb;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	va_list ap;
	va_start(ap, fmt);
	int rc = vsnprintf(_b, _bsz, fmt, ap);
	if(rc >= 0 && (size_t)rc > _bsz)
	{
		if(NULL == (_b = (char*)malloc((size_t)rc + 1)))
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the result buffer");
		rc = vsnprintf(_b, (size_t)rc + 1, fmt, ap);
	}
	va_end(ap);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	if(rc < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "vsnprintf returns an error");

	if(_b == _lb)
	{
		if(ERROR_CODE(int) == pstd_ostream_write(stream, _lb, (size_t)rc))
			ERROR_LOG_GOTO(ERR, "Cannot write the result data to buffer");
	}
	else
	{
		if(ERROR_CODE(int) == pstd_ostream_write_owner_pointer(stream, _b, _printf_buf_free, (size_t)rc))
			ERROR_LOG_GOTO(ERR, "Cannot write pass the data buffer to the stream");
	}

	return 0;
ERR:
	if(_b != _lb) free(_b);
	return ERROR_CODE(int);
}

static int _free(void* mem)
{
	pstd_ostream_t* stream = (pstd_ostream_t*)mem;

	return _ostream_free(stream, 0);
}

static void* _open(const void* ostream)
{
	union {
		const pstd_ostream_t* cos;
		pstd_ostream_t* ret;
	} cvt = {
		.cos = (const pstd_ostream_t*)ostream
	};

	if(cvt.cos->opened == 1)
		ERROR_PTR_RETURN_LOG("Cannot open an ostream twice");

	cvt.ret->opened = 1;

	return cvt.ret;
}

static int _close(void* mem)
{
	(void)mem;
	return 0;
}

static size_t _read(void* __restrict stream_mem, void* __restrict buf, size_t count)
{
	pstd_ostream_t* stream = (pstd_ostream_t*)stream_mem;

	size_t ret = 0;

	while(count > 0 && stream->list_begin != NULL)
	{
		size_t bytes_to_read = count;
		size_t bytes_read = 0;
		int block_exhuated = 0;

		switch(stream->list_begin->type)
		{
			case _BLOCK_TYPE_PAGE:
			{
				if(bytes_to_read > stream->list_begin->page->size - stream->list_begin->page->read)
				{
					bytes_to_read = stream->list_begin->page->size - stream->list_begin->page->read;
					block_exhuated = 1;
				}
				memcpy(buf, stream->list_begin->page->data + stream->list_begin->page->read, bytes_to_read);
				bytes_read = bytes_to_read;
				stream->list_begin->page->read += (uint32_t)bytes_read;
				break;
			}
			case _BLOCK_TYPE_MEMORY:
			{
				if(bytes_to_read > stream->list_begin->memory->size - stream->list_begin->memory->read)
				{
					bytes_to_read = stream->list_begin->memory->size - stream->list_begin->memory->read;
					block_exhuated = 1;
				}
				memcpy(buf, ((char*)stream->list_begin->memory->data) + stream->list_begin->memory->read, bytes_to_read);

				bytes_read = bytes_to_read;

				stream->list_begin->memory->read += (uint32_t)bytes_read;
				break;

			}
			case _BLOCK_TYPE_STREAM:
			{
				bytes_read = pstd_scope_stream_read(stream->list_begin->stream->stream, buf, bytes_to_read);
				if(ERROR_CODE(size_t) == bytes_read)
					ERROR_RETURN_LOG(size_t, "Inner RLS returns a read error");

				if(bytes_read == 0)
				{
					int eos_rc = pstd_scope_stream_eof(stream->list_begin->stream->stream);
					if(ERROR_CODE(int) == eos_rc)
						ERROR_RETURN_LOG(size_t, "Cannot check if the innter RLS reached end-of-stream");
					block_exhuated = (eos_rc > 0);
				}
				break;
			}
		}

		if(block_exhuated)
		{
			_block_t* this = stream->list_begin;
			if(NULL == (stream->list_begin = stream->list_begin->next))
				stream->list_end = NULL;
			if(ERROR_CODE(int) == _block_free(this))
				ERROR_RETURN_LOG(size_t, "Cannot dispose the exhuated data block");
		}
		else if(bytes_read == 0) break;  /* In this case the inner RLS is stall, thus we need to stop at this point */

		ret += bytes_read;
		buf = ((char*)buf) + bytes_read;
		count -= bytes_read;
	}

	return ret;
}

static int _eos(const void* stream_mem)
{
	const pstd_ostream_t* stream = (const pstd_ostream_t*)stream_mem;

	if(stream->list_begin == NULL) return 1;

	if(stream->list_begin == stream->list_end)
	{
		/* If this is the last page, we check this page */
		switch(stream->list_begin->type)
		{
			case _BLOCK_TYPE_PAGE:
				return stream->list_begin->page->read >= stream->list_begin->page->size;
			case _BLOCK_TYPE_MEMORY:
				return stream->list_begin->memory->read >= stream->list_begin->memory->size;
			case _BLOCK_TYPE_STREAM:
				return pstd_scope_stream_eof(stream->list_begin->stream->stream);
		}

		return  ERROR_CODE(int);
	}

	return 0;
}

static int _event(void* __restrict stream_mem, runtime_api_scope_ready_event_t* event_buf)
{
	const pstd_ostream_t* stream = (const pstd_ostream_t*)stream_mem;

	if(stream->list_begin == NULL) return 0;

	switch(stream->list_begin->type)
	{
		case _BLOCK_TYPE_PAGE:
		case _BLOCK_TYPE_MEMORY:
			return 0;
		case _BLOCK_TYPE_STREAM:
			return pstd_scope_stream_ready_event(stream->list_begin->stream->stream, event_buf);
	}

	return ERROR_CODE(int);
}


scope_token_t pstd_ostream_commit(pstd_ostream_t* stream)
{
	if(NULL == stream || stream->commited)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	scope_entity_t ent = {
		.data = stream,
		.free_func = _free,
		.copy_func = NULL,
		.open_func = _open,
		.close_func = _close,
		.eos_func = _eos,
		.read_func = _read,
		.event_func = _event
	};
	
	return pstd_scope_add(&ent);
}
