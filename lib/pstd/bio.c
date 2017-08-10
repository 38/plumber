/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <error.h>
#include <pservlet.h>
#include <pstd.h>

/**
 * @brief the actual type for bufferred IO
 **/
struct _pstd_bio_t {
	uint32_t                 writable:1;         /*!< indicates if this BIO object is writable */
	pipe_t                   pipe;               /*!< the pipe used by this BIO object */
	size_t                   buf_size;           /*!< the size of the BIO buffer */
	char*                    buf;                /*!< the actual buffer */
	size_t                   buf_data_begin;     /*!< the begining of the buffer data */
	size_t                   buf_data_end;       /*!< the end of the buffer data */
	size_t                   bytes_last_read;    /*!< how many bytes last read */
};

pstd_bio_t* pstd_bio_new(pipe_t pipe)
{
	if(ERROR_CODE(pipe_t) == pipe) ERROR_PTR_RETURN_LOG("Invalid arguments");
	pipe_flags_t flags;
	if(pipe_cntl(pipe, PIPE_CNTL_GET_FLAGS, & flags) == ERROR_CODE(int))
	    ERROR_PTR_RETURN_LOG("Cannot read the pipe flags from the buffer");

	//pstd_bio_t* ret = calloc(1, sizeof(pstd_bio_t));
	pstd_bio_t* ret = (pstd_bio_t*)pstd_mempool_alloc(sizeof(pstd_bio_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new BIO object");
	memset(ret, 0, sizeof(pstd_bio_t));

	ret->pipe = pipe;
	ret->buf_size = 4096;   /* TODO make this default size configurable */
	if(NULL == (ret->buf = (char*)malloc(ret->buf_size)))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the BIO buffer");
	ret->buf_data_begin = ret->buf_data_end = 0;
	ret->writable = PIPE_FLAGS_IS_WRITABLE(flags);

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->buf) free(ret->buf);
		free(ret);
	}
	return NULL;
}

int pstd_bio_free(pstd_bio_t* pstd_bio)
{
	int rc = 0;
	if(NULL == pstd_bio) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(pstd_bio->writable && ERROR_CODE(int) == pstd_bio_flush(pstd_bio))
	    rc = ERROR_CODE(int);

	if(!pstd_bio->writable && pstd_bio->buf_data_begin < pstd_bio->buf_data_end)
	    pipe_cntl(pstd_bio->pipe, PIPE_CNTL_EOM,
	              pstd_bio->buf + pstd_bio->buf_data_end - pstd_bio->bytes_last_read,
	              pstd_bio->bytes_last_read + pstd_bio->buf_data_begin - pstd_bio->buf_data_end);

	free(pstd_bio->buf);
	pstd_mempool_free(pstd_bio);
	//free(pstd_bio);
	return rc;
}
static inline int _flush(pstd_bio_t* pstd_bio, int all)
{
	int ret = 0;
	size_t buf_size = pstd_bio->buf_data_end - pstd_bio->buf_data_begin;
	for(;buf_size > 0 && ret == 0;)
	{
		size_t rc = pipe_write(pstd_bio->pipe, pstd_bio->buf + pstd_bio->buf_data_begin, buf_size);
		if(!all && rc == 0) break;
		if(ERROR_CODE(size_t) == rc) ret = ERROR_CODE(int);
		else
		{
			pstd_bio->buf_data_begin += rc;
			buf_size -= rc;
		}
	}

	if(pstd_bio->buf_data_end - pstd_bio->buf_data_begin > 0)
	    memmove(pstd_bio->buf, pstd_bio->buf + pstd_bio->buf_data_begin, pstd_bio->buf_data_end - pstd_bio->buf_data_begin);

	pstd_bio->buf_data_end = pstd_bio->buf_data_end - pstd_bio->buf_data_begin;
	pstd_bio->buf_data_begin = 0;

	return ret;
}
int pstd_bio_flush(pstd_bio_t* pstd_bio)
{
	if(NULL == pstd_bio) ERROR_RETURN_LOG(int, "Invalid arguments");

	/* If the BIO object isn't writable we do not actually flush the buffer */
	if(!pstd_bio->writable) return 0;

	return _flush(pstd_bio, 1);
}

pipe_t pstd_bio_pipe(pstd_bio_t* pstd_bio)
{
	if(NULL == pstd_bio) ERROR_RETURN_LOG(pipe_t, "Invalid arguments");
	return pstd_bio->pipe;
}

int pstd_bio_set_buf_size(pstd_bio_t* pstd_bio, size_t size)
{
	if(NULL == pstd_bio) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(size > pstd_bio->buf_size)
	{
		char* new_buf = (char*)realloc(pstd_bio->buf, size);
		if(NULL == new_buf) ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the existing internal buffer");
		pstd_bio->buf = new_buf;
		LOG_DEBUG("The existing buffer of size %zu has been resized to size %zu", pstd_bio->buf_size, size);
	}

	pstd_bio->buf_size = size;
	return 0;
}

static inline size_t  _get_bufferred_data(pstd_bio_t* pstd_bio, size_t expected_size, void const ** data)
{
	if(pstd_bio->buf_data_begin >= pstd_bio->buf_data_end)
	{
		if(expected_size > pstd_bio->buf_size)
		{
			/* In this case, bufferring is not helpful at all, so just return 0 */
			return 0;
		}
		/* If there's no more data, read data to the buffer */
		pstd_bio->buf_data_begin = 0;
		if(ERROR_CODE(size_t) == (pstd_bio->bytes_last_read = pstd_bio->buf_data_end = pipe_read(pstd_bio->pipe, pstd_bio->buf, pstd_bio->buf_size)))
		    ERROR_RETURN_LOG(size_t, "Cannot read from the pipe");
	}

	void* ret = pstd_bio->buf + pstd_bio->buf_data_begin;

	if(pstd_bio->buf_data_end - pstd_bio->buf_data_begin < expected_size)
	    expected_size = pstd_bio->buf_data_end - pstd_bio->buf_data_begin;

	if(expected_size == 0) return 0;
	*data = ret;
	pstd_bio->buf_data_begin += expected_size;
	return expected_size;
}

size_t pstd_bio_read(pstd_bio_t* pstd_bio, void* ptr, size_t size)
{
	if(NULL == pstd_bio || NULL == ptr) ERROR_RETURN_LOG(size_t, "Invalid arguments");

	if(pstd_bio->writable) ERROR_RETURN_LOG(size_t, "Cannot read from an output BIO object");

	const void* data;
	size_t ret = _get_bufferred_data(pstd_bio, size, &data);
	if(ERROR_CODE(size_t) == ret)
	    ERROR_RETURN_LOG(size_t, "Cannot read from BIO read buffer");
	memcpy(ptr, data, ret);
	size -= ret;

	for(;size > 0 && !pipe_eof(pstd_bio->pipe);)
	{
		size_t rc = pipe_read(pstd_bio->pipe, ptr + ret, size);
		if(ERROR_CODE(size_t) == rc) ERROR_RETURN_LOG(size_t, "Cannot read data from pipe");
		if(rc == 0) return ret;
		size -= rc;
		ret += rc;
	}

	return ret;
}

int pstd_bio_getc(pstd_bio_t* pstd_bio, char* ch)
{
	if(NULL == pstd_bio || NULL == ch) ERROR_RETURN_LOG(int, "Invalid arguments");

	const void* data;
	size_t ret = _get_bufferred_data(pstd_bio, 1, &data);
	if(ERROR_CODE(size_t) == ret) ERROR_RETURN_LOG(int, "Cannot read byte from BIO buffer");
	if(ret == 0) return 0;

	*ch = *(const char*)data;
	return 1;
}

int pstd_bio_eof(pstd_bio_t* pstd_bio)
{
	if(NULL == pstd_bio) ERROR_RETURN_LOG(int, "Invalid arguments");

	return pipe_eof(pstd_bio->pipe);
}

static inline size_t _write_buffer(pstd_bio_t* pstd_bio, const void* ptr, size_t size)
{
	size_t bytes_can_write = pstd_bio->buf_size - pstd_bio->buf_data_end;
	if(bytes_can_write > size) bytes_can_write = size;
	memcpy(pstd_bio->buf + pstd_bio->buf_data_end, ptr, bytes_can_write);
	pstd_bio->buf_data_end += bytes_can_write;
	size -= bytes_can_write;

	if(pstd_bio->buf_data_end == pstd_bio->buf_size && _flush(pstd_bio, 0) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(size_t, "Cannot write the buffer to pipe");

	return bytes_can_write;
}

size_t pstd_bio_write(pstd_bio_t* pstd_bio, const void* ptr, size_t size)
{
	size_t ret = 0;
	if(NULL == pstd_bio || NULL == ptr) ERROR_RETURN_LOG(size_t, "Invalid arguments");

	/* If the buffer contains existing data or the data to write is smaller than the buffer,
	 * we try to write to the buffer first */
	if(pstd_bio->buf_data_begin < pstd_bio->buf_data_end || size < pstd_bio->buf_size - pstd_bio->buf_data_end)
	{
		size_t rc = _write_buffer(pstd_bio, ptr, size);
		if(ERROR_CODE(size_t) == rc) ERROR_RETURN_LOG(size_t, "Cannot write data to the buffer");

		ptr = ((const char*)ptr) + rc;
		size -= rc;
		ret += rc;
	}

	if(size > 0)
	{
		if(size <= pstd_bio->buf_size - pstd_bio->buf_data_end)
		{
			size_t rc = _write_buffer(pstd_bio, ptr, size);
			if(ERROR_CODE(size_t) == rc) ERROR_RETURN_LOG(size_t, "Cannot write data to the buffer");
			ret += rc;
		}
		else
		{
			size_t rc = pipe_write(pstd_bio->pipe, ptr, size);
			if(ERROR_CODE(size_t) == rc) ERROR_RETURN_LOG(size_t, "Cannot write to the pipe");
			ret += rc;
		}
	}

	return ret;
}

size_t pstd_bio_vprintf(pstd_bio_t* pstd_bio, const char* fmt, va_list ap)
{
	char _b[1024];
	size_t _bsz = sizeof(_b);
	size_t ret = 0;
	
	int rc = vsnprintf(_b, _bsz, fmt, ap);
	if(rc < 0) ERROR_RETURN_LOG_ERRNO(size_t, "vsnprintf returns an error");
	
	if(ret != ERROR_CODE(size_t))
	{
		size_t bytes_to_write = (size_t)rc;
		const char* p = _b;
		while(bytes_to_write > 0)
		{
			size_t rc = pstd_bio_write(pstd_bio, p, bytes_to_write);
			if(ERROR_CODE(size_t) == rc) ERROR_RETURN_LOG(size_t, "Cannot write to the BIO object");
			bytes_to_write -= rc;
			p += rc;
		}
	}

	return ret;

}

size_t pstd_bio_printf(pstd_bio_t* pstd_bio, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	size_t ret = pstd_bio_vprintf(pstd_bio, fmt, ap);
	va_end(ap);

	return ret;
}

size_t pstd_bio_puts(pstd_bio_t* pstd_bio, const char* str)
{
	if(NULL == pstd_bio || NULL == str) ERROR_RETURN_LOG(size_t, "Invalid arguments");
	size_t ret = 0;
	size_t bytes_to_write = strlen(str);
	while(bytes_to_write > 0)
	{
		size_t rc = pstd_bio_write(pstd_bio, str, bytes_to_write);
		if(ERROR_CODE(size_t) == rc) ERROR_RETURN_LOG(size_t, "Cannot write to BIO object");
		bytes_to_write -= rc;
		str += rc;
		ret += rc;
	}

	return ret;
}

int pstd_bio_putc(pstd_bio_t* pstd_bio, char ch)
{
	if(NULL == pstd_bio) ERROR_RETURN_LOG(int, "Invalid arguments");

	for(;;)
	{
		size_t rc = pstd_bio_write(pstd_bio, &ch, 1);
		if(rc == ERROR_CODE(size_t)) ERROR_RETURN_LOG(int, "Cannot write to BIO object");
		if(rc == 1) return 0;
	}
}

static inline size_t _data_req_handle(void* __restrict ctx, const void* __restrict data, size_t size)
{
	pstd_bio_t* bio = (pstd_bio_t*)ctx;

	memcpy(bio->buf + bio->buf_data_end, data, size);
	bio->buf_data_end += size;

	if(bio->buf_size <= bio->buf_data_end && ERROR_CODE(int) == pstd_bio_flush(bio))
	    ERROR_RETURN_LOG(size_t, "Cannot flush the BIO buffer");

	return size;
}

int pstd_bio_write_scope_token(pstd_bio_t* pstd_bio, scope_token_t token)
{
	if(NULL == pstd_bio || ERROR_CODE(scope_token_t) == token)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

#if 0
	/* Flush the prevoius bytes first, because we want to preserve the byte order */
	if(ERROR_CODE(int) == pstd_bio_flush(pstd_bio))
	    ERROR_RETURN_LOG(int, "Cannot flush the BIO buffer");
#endif

	scope_token_data_req_t data_req = {
		.size =  pstd_bio->buf_size - pstd_bio->buf_data_end,
		.context = pstd_bio,
		.data_handler = _data_req_handle
	};

	/* Then directly initialize the scope token write call */
	if(ERROR_CODE(int) == pipe_write_scope_token(pstd_bio->pipe, token, &data_req))
	    ERROR_RETURN_LOG(int, "Cannot write the scope token to pipe");

	return 0;
}
