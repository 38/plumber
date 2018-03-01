/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <pservlet.h>

#include <pstd/types/trans.h>
#include <pstd/mempool.h>
#include <pstd/scope.h>

struct _pstd_trans_t {
	uint32_t             commited:1;      /*!< Indicates if the token is committed */
	uint32_t             opened:1;        /*!< Indicats if this transformer has been opened alreadly */
	uint32_t             wait_feed:1;     /*!< Indicates if we are waiting for data feed */
	uint32_t             data_source_eos:1; /*!< Indicates if we have seen the EOS from the data source */
	pstd_trans_desc_t    ctx;             /*!< The transformer data */
	scope_token_t        src_token;       /*!< The source RLS token */
	void*                stream_proc;     /*!< The stream processor instance */
	pstd_scope_stream_t* data_source;     /*!< The data source stream */
	char*                origin_buf;      /*!< The original data buffer */
	size_t               origin_buf_cap;  /*!< The capacity of the original buffe */
	size_t               origin_buf_size; /*!< The actual number of bytes that is avaiable in the buffer */
	size_t               origin_buf_used; /*!< The number of bytes that has been processed already */
};

static size_t _default_buf_size = 0;

pstd_trans_t* pstd_trans_new(scope_token_t token, pstd_trans_desc_t desc)
{
	if(token == 0 || token == ERROR_CODE(scope_token_t))
		ERROR_PTR_RETURN_LOG("Invalid scope token");

	if(desc.init_func == NULL || desc.feed_func == NULL || desc.fetch_func == NULL || desc.cleanup_func == NULL)
		ERROR_PTR_RETURN_LOG("Undefined callback functions");

	pstd_trans_t* ret = (pstd_trans_t*)pstd_mempool_alloc(sizeof(pstd_trans_t));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the stream processor");

	memset(ret, 0, sizeof(pstd_trans_t));

	ret->ctx = desc;
	ret->src_token = token;

	if(_default_buf_size == 0)
		_default_buf_size = (size_t)getpagesize();

	ret->origin_buf_cap = _default_buf_size;

	return ret;
}

int pstd_trans_set_buffer_size(pstd_trans_t* trans, size_t size)
{
	if(NULL == trans || size == 0 || size == ERROR_CODE(size_t))
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(trans->opened)
		ERROR_RETURN_LOG(int, "Cannot change the buffer size for a opened stream transformer");

	if(size < _default_buf_size) size = _default_buf_size;

	trans->origin_buf_cap = size;

	return 0;
}

static int _free_impl(pstd_trans_t* trans, int app_space)
{
	if(app_space && trans->commited)
		ERROR_RETURN_LOG(int, "Cannot dispose a token has already commited");

	int rc = 0;

	if(trans->stream_proc != NULL && ERROR_CODE(int) == trans->ctx.cleanup_func(trans->stream_proc))
		rc = ERROR_CODE(int);

	if(NULL != trans->data_source &&  ERROR_CODE(int) == pstd_scope_stream_close(trans->data_source))
		rc = ERROR_CODE(int);

	if(NULL != trans->origin_buf)
	{
		if(trans->origin_buf_cap == _default_buf_size && ERROR_CODE(int) == pstd_mempool_page_dealloc(trans->origin_buf))
			rc = ERROR_CODE(int);
		if(trans->origin_buf_cap != _default_buf_size)
			free(trans->origin_buf);
	}

	return rc;
}

int pstd_trans_free(pstd_trans_t* trans)
{
	return _free_impl(trans, 1);
}

static int _free(void* mem)
{
	return _free_impl((pstd_trans_t*)mem, 0);
}

static void* _open(const void* trans)
{
	union {
		const pstd_trans_t* trans;
		pstd_trans_t* stream;
	} cvt = { .trans = (const pstd_trans_t*)trans };

	pstd_trans_t* ret = cvt.stream;

	char* pooled = NULL;
	char* malloced = NULL;

	if(ret->opened) 
		ERROR_PTR_RETURN_LOG("Cannot open a stream transformer RLS twice");

	ret->opened = 1;
	ret->data_source_eos = 0;

	if(ret->origin_buf_cap == _default_buf_size)
		ret->origin_buf = pooled = (char*)pstd_mempool_page_alloc();
	else
		ret->origin_buf = malloced = (char*)malloc(ret->origin_buf_size);

	if(ret->origin_buf == NULL)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate the original data buffer for the stream processor");

	if(NULL == (ret->data_source = pstd_scope_stream_open(ret->src_token)))
		ERROR_PTR_RETURN_LOG("Cannot open the data source stream");

	if(NULL == (ret->stream_proc = ret->ctx.init_func(ret->ctx.data)))
		ERROR_LOG_GOTO(ERR, "Cannot initialize the stream processor instance");

	return ret;

ERR:
	if(NULL != ret->data_source) pstd_scope_stream_close(ret->data_source);
	if(NULL != ret->stream_proc) ret->ctx.cleanup_func(ret->stream_proc);
	if(NULL != pooled) pstd_mempool_page_dealloc(pooled);
	if(NULL != malloced) free(malloced);

	return NULL;
}

static int _close(void* mem)
{
	(void)mem;
	return 0;
}

static size_t _read(void* __restrict trans_mem, void* __restrict buf, size_t count)
{
	pstd_trans_t* trans = (pstd_trans_t*)trans_mem;

	size_t ret = 0;

	/* First we need to exhuast the previous unread data from the stream processor */
	for(;count > 0 && !trans->wait_feed;)
	{
		size_t bytes_read = trans->ctx.fetch_func(trans->stream_proc, buf, count);

		if(ERROR_CODE(size_t) == bytes_read)
			ERROR_RETURN_LOG(size_t, "Cannot fetch bytes from the stream processor");

		buf = ((int8_t*)buf) + bytes_read;
		count -= bytes_read;
		ret += bytes_read;

		/* The convention is once we see a 0 bytes returned we will assume the stream processor internal buffer is read up */
		if(bytes_read == 0)
		{
			trans->wait_feed = 1;
			break;
		}
	}


	/* Then we need to fetch original bytes from the data source to the origianl buffer and send them to the stream processor */
	for(;count > 0;)
	{
		if(trans->origin_buf_size <= trans->origin_buf_used)
		{
			/* We waiting for feed plus we are in the data source end make us exit for now */
			if(trans->data_source_eos) return ret;

			/* If we don't have any original bytes, try to grab some */
			size_t bytes_read = pstd_scope_stream_read(trans->data_source, trans->origin_buf, trans->origin_buf_cap);

			if(bytes_read == ERROR_CODE(size_t))
				ERROR_RETURN_LOG(size_t, "Cannot read original bytes from the RLS data source");

			/* If we cannot get anything, just return */
			if(bytes_read == 0)
			{
				int eos_rc = pstd_scope_stream_eof(trans->data_source);
				if(ERROR_CODE(int) == eos_rc)
					ERROR_RETURN_LOG(size_t, "Cannot check if the data source reached the end");

				/* If we may have more data, obviously we need to return 0 which indicates the wait state */
				if(eos_rc)
				{

					trans->data_source_eos = 1;

					if(ERROR_CODE(size_t) == trans->ctx.feed_func(trans->stream_proc, NULL, 0))
						ERROR_RETURN_LOG(size_t, "Cannot send the end of data source message");

					trans->wait_feed = 0;

					/* Since we changed the state of the stream processor, let's try one more time */
					goto FETCH;
				}

				/* So we just stop at this point and needs to wait for next iteration */
				return ret;
			}

			trans->origin_buf_size = bytes_read;
			trans->origin_buf_used = 0;
		}

		size_t bytes_accepted = trans->ctx.feed_func(trans->stream_proc, 
				                                     trans->origin_buf + trans->origin_buf_used, 
													 trans->origin_buf_size - trans->origin_buf_used);

		if(ERROR_CODE(size_t) == bytes_accepted || bytes_accepted > trans->origin_buf_size - trans->origin_buf_used)
			ERROR_RETURN_LOG(size_t, "Unexpected feed callback return value");

		/* The the stream processor refuse to accept any bytes we just return */
		if(bytes_accepted == 0)
			return ret;
		trans->origin_buf_used = 0;
		trans->origin_buf_used += bytes_accepted;

FETCH:

		/* Then we need to fetch all the result bytes if possible */
		for(;count > 0;)
		{
			size_t bytes_fetched = trans->ctx.fetch_func(trans->stream_proc, buf, count);

			if(ERROR_CODE(size_t) == bytes_fetched || bytes_fetched > count)
				ERROR_RETURN_LOG(size_t, "The fetch callback returns unexpected value");

			/* If the stream processor return 0, we just grab more data and try again */
			if(bytes_fetched == 0) 
			{
				trans->wait_feed = 1;
				break;
			}

			trans->wait_feed = 0;

			count -= bytes_fetched;
			buf = ((int8_t*)buf) + bytes_fetched;
			ret += bytes_fetched;
		}
	}

	return ret;
}

static int _eos(const void* trans_mem)
{
	const pstd_trans_t* trans = (const pstd_trans_t*)trans_mem;

	return trans->wait_feed && trans->data_source_eos;
}

static int _event(void* __restrict trans_mem, runtime_api_scope_ready_event_t* event_buf)
{
	pstd_trans_t* trans = (pstd_trans_t*)trans_mem;

	if(trans->wait_feed) 
		return pstd_scope_stream_ready_event(trans->data_source, event_buf);

	return 0;
}

scope_token_t pstd_trans_commit(pstd_trans_t* trans)
{
	if(NULL == trans || trans->commited)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	scope_entity_t ent = {
		.data = trans,
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
