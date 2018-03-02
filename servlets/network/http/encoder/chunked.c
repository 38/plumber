/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <pstd/mempool.h>
#include <pstd/types/trans.h>

#include <chunked.h>

typedef struct {
	uint32_t  chunck_size;     /*!< The size of current chunck */
	char      size_buf[16];    /*!< The buffer used for the chunck size */
	uint8_t   size_length:4;   /*!< The length of the size */
	uint8_t   size_written:4;  /*!< The written size string length */
	uint8_t   page_limit;      /*!< The maximum number of pages we can use */
	uint32_t  no_more:1;       /*!< The data source has no more data */
	uint32_t  trailer_state:7; /*!< The trailer state */
	uint32_t  current_offset;  /*!< The page offset */
	uintpad_t __padding__[0];
	char*     pages[0];        /*!< The actual pages */
} _processor_t;

static uint32_t _page_size = 0;

static pstd_trans_inst_t* _init(void* data)
{
	uint8_t page_limit = (uint8_t)(uintptr_t)data;

	size_t size = sizeof(_processor_t) + sizeof(char*) * page_limit;

	_processor_t* proc = pstd_mempool_alloc((uint32_t)size);
	if(NULL == proc)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the chunked stream processor");

	memset(proc, 0, size);

	proc->page_limit = page_limit;

	uint8_t i;
	for(i = 0; i < page_limit; i ++)
		if(NULL == (proc->pages[i] = pstd_mempool_page_alloc()))
			ERROR_LOG_GOTO(ERR, "Cannot allocate the buffer page");
	return (pstd_trans_inst_t*)proc;
ERR:
	for(i = 0; i < size; i ++)
		if(NULL != proc->pages[i])
			pstd_mempool_page_dealloc(proc->pages[i]);
	pstd_mempool_free(proc);
	return NULL;
}

static size_t _feed(pstd_trans_inst_t* __restrict stream_proc, const void* __restrict in, size_t size)
{
	_processor_t* proc = (_processor_t*)stream_proc;

	if(NULL  == in)
	{
		proc->no_more = 1;
		return 0;
	}

	size_t ret = 0;

	uint32_t bytes_to_write = proc->page_limit * _page_size - proc->chunck_size;

	if(bytes_to_write > size)
		bytes_to_write = (uint32_t)size;

	while(bytes_to_write > 0)
	{
		uint32_t cur_block = proc->chunck_size / _page_size;
		uint32_t cur_offset = proc->chunck_size % _page_size;
		
		uint32_t bytes_to_copy = _page_size - cur_offset;
		if(bytes_to_copy > size) bytes_to_copy = (uint32_t)size;

		memcpy(proc->pages[cur_block] + cur_offset, in, bytes_to_copy);

		proc->chunck_size += bytes_to_copy;
		bytes_to_write -= bytes_to_copy;
		in = ((const char*)in) + bytes_to_copy;
		ret += bytes_to_copy;
	}

	return ret;
}

static size_t _fetch(pstd_trans_inst_t* __restrict stream_proc, void* __restrict out, size_t size)
{
	_processor_t* proc = (_processor_t*)stream_proc;

	if(!proc->no_more && proc->page_limit * _page_size != proc->chunck_size)
		return 0;

	if(proc->size_length == 0)
	{
		uint32_t blk_size = proc->chunck_size;
		if(blk_size == 0) memcpy(proc->size_buf, "\n\r0", 3), proc->size_length = 3;
		else
		{
			memcpy(proc->size_buf, "\n\r", 2);
			proc->size_length = 2;
			for(;blk_size;blk_size /= 16)
			{
				char cur = (char)(blk_size % 16);
				if(cur < 10) 
					proc->size_buf[proc->size_length ++] = (char)('0' + cur);
				else
					proc->size_buf[proc->size_length ++] = (char)('A' + cur - 10);
			}
		}
	}

	size_t ret = 0;

	for(;proc->size_length != proc->size_written && size > 0; out = ((char*)out) + 1, size --, ret ++)
		*(char*)out = proc->size_buf[proc->size_length - (proc->size_written ++) - 1];

	while(size > 0 && proc->chunck_size > proc->current_offset)
	{
		uint32_t cur_block = proc->current_offset / _page_size;
		uint32_t cur_offset = proc->current_offset % _page_size;

		uint32_t bytes_to_copy = _page_size - cur_offset;
		if(bytes_to_copy > proc->chunck_size - proc->current_offset)
			bytes_to_copy = proc->chunck_size - proc->current_offset;
		if(bytes_to_copy > size) bytes_to_copy = (uint32_t)size;

		memcpy(out, proc->pages[cur_block] + cur_offset, bytes_to_copy);

		proc->current_offset += bytes_to_copy;
		ret += bytes_to_copy;
		size -= bytes_to_copy;
		out = ((char*)out) + bytes_to_copy;
	}

	if(ret == 0)
	{
		if(proc->chunck_size >= 2 && 
		   proc->pages[(proc->chunck_size - 2)/_page_size][(proc->chunck_size - 2)%_page_size] == '\r' &&
		   proc->pages[(proc->chunck_size - 1)/_page_size][(proc->chunck_size - 1)%_page_size] == '\n')
			proc->trailer_state = 2;
		while(size > 0)
		{
			switch(proc->trailer_state)
			{
				case 0:
				case 1:
					*(char*)out = proc->trailer_state == 0 ? '\r' : '\n';
					out = ((char*)out) + 1;
					proc->trailer_state ++;
					size --;
					ret ++;
					break;
				case 2:
				case 3:
				case 4:
				case 5:
				case 6:
					if(!proc->no_more) 
						proc->trailer_state = 0x7f;
					else
					{
						*(char*)out = "0\r\n\r\n"[proc->trailer_state - 2];
						out = ((char*)out) + 1;
						size --;
						ret ++;
						proc->trailer_state ++;
					}
					break;
				default:
					proc->chunck_size = 0;
					proc->current_offset = 0;
					proc->trailer_state = 0;
					proc->size_length = 0;
					proc->size_written = 0;
					proc->no_more = 0;
					goto EXIT;
			}
		}
	}

EXIT:
	return ret;
}

static int _cleanup(pstd_trans_inst_t* stream_proc)
{
	int rc = 0;
	_processor_t* proc = (_processor_t*)stream_proc;

	uint32_t i;

	for(i = 0; i < proc->page_limit; i ++)
		if(NULL != proc->pages[i])
			if(ERROR_CODE(int) == pstd_mempool_page_dealloc(proc->pages[i]))
				rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == pstd_mempool_free(proc))
		rc = ERROR_CODE(int);

	return rc;
}

scope_token_t chunked_encode(scope_token_t token, uint8_t chunked_pages)
{
	if(ERROR_CODE(scope_token_t) == token || 0 == chunked_pages)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	if(_page_size == 0) _page_size = (uint32_t)getpagesize();

	pstd_trans_desc_t desc = {
		.data = ((char*)0) + chunked_pages,
		.init_func = _init,
		.feed_func = _feed,
		.fetch_func = _fetch,
		.cleanup_func = _cleanup
	};

	pstd_trans_t* trans = pstd_trans_new(token, desc);
	if(NULL == trans)
		ERROR_RETURN_LOG(scope_token_t, "Cannot create stream processor object");

	scope_token_t result = pstd_trans_commit(trans);

	if(ERROR_CODE(scope_token_t) == result)
	{
		pstd_trans_free(trans);
		return ERROR_CODE(scope_token_t);
	}

	return result;
}
