/**
 * Copyright (C) 2018, Hao Hou
 **/
#if HAS_ZLIB
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>

#include <pservlet.h>
#include <pstd.h>

#include <pstd/mempool.h>
#include <pstd/types/trans.h>

#include <zlib_token.h>

typedef struct {
	uint32_t   data_source_eos:1;      /*!< Indicates we see end-of-stream marker from the data source */

	z_stream*  zlib_stream;         /*!< The zlib stream we are dealing with */
	char*      input_buf;           /*!< The input buffer */
	int        last_zlib_ret;       /*!< The last zlib return value */
} _processor_t;

static uint32_t _page_size = 0;

static pstd_trans_inst_t* _init(void* data)
{
	z_stream* zs = (z_stream*)data;
	_processor_t* ret = (_processor_t*)pstd_mempool_alloc(sizeof(_processor_t));

	if(NULL == ret)
		ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the processor");

	memset(ret, 0, sizeof(*ret));

	ret->zlib_stream = zs;

	if(NULL == (ret->input_buf = (char*)pstd_mempool_page_alloc()))
		ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the input buffer");

	ret->last_zlib_ret = Z_STREAM_END;

	ret->zlib_stream->avail_in = 0;

	return (pstd_trans_inst_t*)ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->input_buf)
			pstd_mempool_page_dealloc(ret->input_buf);
		pstd_mempool_free(ret);
	}
	if(NULL != zs)
	{
		deflateEnd(zs);
		pstd_mempool_free(zs);
	}
	return NULL;
}

static size_t _feed(pstd_trans_inst_t* __restrict stream_proc, const void* __restrict in, size_t size)
{
	_processor_t* proc = (_processor_t*)stream_proc;

	if(NULL == in)
	{
		proc->data_source_eos = 1;
		return 0;
	}

	if(proc->zlib_stream->avail_in > 0 && proc->last_zlib_ret != Z_OK)
		return 0;

	size_t ret = size;
	if(ret > _page_size)
		ret = _page_size;

	memcpy(proc->input_buf, in, ret);

	proc->zlib_stream->next_in = (uint8_t*)proc->input_buf;
	proc->zlib_stream->avail_in = (uint32_t)ret;

	proc->last_zlib_ret = Z_OK;

	return ret;
}

static size_t _fetch(pstd_trans_inst_t* __restrict stream_proc, void* __restrict out, size_t size)
{
	_processor_t* proc = (_processor_t*)stream_proc;

	if(proc->last_zlib_ret == Z_STREAM_END)
		return 0;

	proc->zlib_stream->next_out = (uint8_t*)out;
	proc->zlib_stream->avail_out = (uint32_t)size;

	proc->last_zlib_ret = deflate(proc->zlib_stream, proc->data_source_eos ? Z_FINISH : Z_NO_FLUSH);

	if(proc->last_zlib_ret == Z_STREAM_ERROR)
		ERROR_RETURN_LOG(size_t, "Zlib returns an error: %s", proc->zlib_stream->msg);

	return (size_t)(size - proc->zlib_stream->avail_out);
}

static int _cleanup(pstd_trans_inst_t* stream_proc)
{
	int ret = 0;
	_processor_t* proc = (_processor_t*)stream_proc;
	if(NULL != proc->input_buf && ERROR_CODE(int) == pstd_mempool_page_dealloc(proc->input_buf))
		ret = ERROR_CODE(int);

	if(ERROR_CODE(int) == pstd_mempool_free(proc))
		ret = ERROR_CODE(int);

	if(NULL != proc->zlib_stream)
	{
		if(Z_OK != deflateEnd(proc->zlib_stream))
			ret = ERROR_CODE(int);
		if(ERROR_CODE(int) == pstd_mempool_free(proc->zlib_stream))
			ret = ERROR_CODE(int);
	}
	return ret;
}

scope_token_t zlib_token_encode(scope_token_t data_token, zlib_token_format_t format, int level)
{
	if(_page_size == 0) _page_size = (uint32_t)getpagesize();

	if(ERROR_CODE(scope_token_t) == data_token || 0 == data_token || level < 0 || level > 9)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	z_stream* zs = NULL;

	if(NULL == (zs = (z_stream*)pstd_mempool_alloc(sizeof(z_stream))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the z_stream");

	zs->zalloc = Z_NULL;
	zs->zfree  = Z_NULL;
	zs->opaque = Z_NULL;

	switch(format)
	{
		case ZLIB_TOKEN_FORMAT_DEFLATE:
			if(Z_OK != deflateInit(zs, level))
				ERROR_LOG_GOTO(ERR, "Cannot initialize the zlib for deflate algoritm");
			break;
		case ZLIB_TOKEN_FORMAT_GZIP:
			if(Z_OK != deflateInit2(zs, level, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY))
				ERROR_LOG_GOTO(ERR, "Cannot itnialize the zlib for gzip");
			break;
		default:
		    ERROR_LOG_GOTO(ERR, "Invalid format option");
	}

	pstd_trans_desc_t desc = {
		.data = zs,
		.init_func = _init,
		.feed_func = _feed,
		.fetch_func = _fetch,
		.cleanup_func = _cleanup
	};

	pstd_trans_t* trans = pstd_trans_new(data_token, desc);
	if(NULL == trans)
		ERROR_RETURN_LOG(scope_token_t, "Cannot create stream processor object");

	scope_token_t result = pstd_trans_commit(trans);

	if(ERROR_CODE(scope_token_t) == result)
	{
		pstd_trans_free(trans);
		return ERROR_CODE(scope_token_t);
	}

	return result;
ERR:
	pstd_mempool_free(zs);
	return ERROR_CODE(scope_token_t);
}
#endif
