/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <curl/curl.h>

#include <barrier.h>

#include <pstd.h>
#include <rls.h>
#include <client.h>

#define RLS_PAGE_QUEUE_SIZE 32

#define _PAGESIZE 4096

/**
 * @brief The actual data structure for the CURL rls data object
 **/
struct _rls_obj_t {
	volatile uint32_t committed:1;                      /*!< If this object has been commited to RLS */
	uint32_t          opened:1;                         /*!< Since this kinds of token throughs the data away once its consumed, so we shouldn't allow the token opened twice */
	uint32_t          abort:1;                          /*!< If the transmission has be aborted */
	volatile uint32_t curl_waiting:1;                   /*!< Indicates that the curl is waiting for write callback */
	CURL*             curl_handle;                      /*!< The CURL handle associated to this RLS object */	
	uint32_t       pq_front_idx;                        /*!< The front index of the page queue */ 
	uint32_t       pq_front_ofs;                        /*!< The offset of the queue head */
	void*          data_pages[RLS_PAGE_QUEUE_SIZE];     /*!< The actual data pages we are using.*/
	uint32_t       pq_rear_ofs;                         /*!< The offset of the queue tail */
	uint32_t       pq_rear_idx;                         /*!< The rear index of the page queue */
	volatile uint32_t curl_using:1;                     /*!< Indicates if this is using by curl */
};

STATIC_ASSERTION_LE_ID(obj_smaller_than_page, sizeof(rls_obj_t), _PAGESIZE);

rls_obj_t* rls_obj_new(CURL* curl_handle)
{
	if(NULL == curl_handle)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	rls_obj_t* obj = pstd_mempool_alloc(sizeof(rls_obj_t));

	if(NULL == obj) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new RLS object");

	obj->committed = 0;
	obj->opened   = 0;
	obj->curl_waiting = 0;
	obj->curl_handle = curl_handle;
	memset(obj->data_pages, 0, sizeof(obj->data_pages));
	obj->pq_front_idx = 0;
	obj->pq_front_ofs = 0;
	obj->pq_rear_idx  = 0;
	obj->pq_rear_ofs  = 0;
	obj->abort = 0;
	obj->curl_using  = 0;
	obj->curl_using = 0;

	return obj;
}

static inline int _obj_free_impl(rls_obj_t* obj)
{
	int rc = 0;
	uint32_t i;
	for(i = 0; i < RLS_PAGE_QUEUE_SIZE; i ++)
		if(NULL != obj->data_pages[i] && ERROR_CODE(int) == pstd_mempool_page_dealloc(obj->data_pages[i]))
		{
			LOG_ERROR("Cannot dispose page @%p", obj->data_pages[i]);
			rc = ERROR_CODE(int);
		}

	if(ERROR_CODE(int) == pstd_mempool_free(obj))
		ERROR_RETURN_LOG(int, "Cannot dispose RLS object @%p", obj);

	return rc;
}

int rls_obj_free(rls_obj_t* obj, int from_curl)
{
	if(NULL == obj) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(from_curl && obj->committed)
	{
		/* If the object is already in RLS, just reset the flag */
		obj->curl_using = 0;
		return 0;
	}

	if(obj->committed) ERROR_RETURN_LOG(int, "Trying to dispose a committed RLS object");

	if(!from_curl && obj->curl_using) ERROR_RETURN_LOG(int, "Trying to dispose a RLS object using by CURL");

	return _obj_free_impl(obj);
}

int rls_obj_write(rls_obj_t* obj, const void* data, size_t count)
{
	if(NULL == obj || NULL == data)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t n_free_pages = RLS_PAGE_QUEUE_SIZE - (obj->pq_rear_idx - obj->pq_front_idx);
	uint32_t n_free_bytes = (obj->pq_rear_idx != obj->pq_front_idx) ? _PAGESIZE - obj->pq_rear_ofs : 0;

	if(n_free_pages & 0x80000000) n_free_pages = 0;

	if(n_free_pages * _PAGESIZE + n_free_bytes < count)
	{
		LOG_DEBUG("The write buffer is full, stop recieving data");
		obj->curl_waiting = 1;
		return 0;
	}

	if(n_free_bytes > 0)
	{
		LOG_DEBUG("Current page buffer has %u unused bytes, try to use it first", n_free_bytes);

		uint32_t bytes_to_copy = n_free_bytes;

		if(bytes_to_copy > count) bytes_to_copy = (uint32_t)count;

		memcpy((char*)obj->data_pages[obj->pq_rear_idx & (RLS_PAGE_QUEUE_SIZE - 1)] + obj->pq_rear_ofs, data, bytes_to_copy);

		data = (const char*)data + bytes_to_copy;
		count -= bytes_to_copy;

		BARRIER();

		if(count == 0) 
			obj->pq_rear_ofs += bytes_to_copy;
		else
		{
			obj->pq_rear_ofs = 0;
			BARRIER();
			obj->pq_rear_idx ++;
		}
	}

	while(count > _PAGESIZE)
	{
		LOG_DEBUG("Current data chunck is larger than a page, copy it to the next page");

		memcpy(obj->data_pages[obj->pq_rear_idx & (RLS_PAGE_QUEUE_SIZE - 1)], data, _PAGESIZE);

		count -= _PAGESIZE;
		data = (const char*)data + _PAGESIZE;

		BARRIER();

		obj->pq_rear_idx ++;
	}

	if(count > 0)
	{
		LOG_DEBUG("Write data to the new page");

		memcpy(obj->data_pages[obj->pq_rear_idx & (RLS_PAGE_QUEUE_SIZE - 1)], data, count);

		BARRIER();

		obj->pq_rear_ofs = (uint32_t)count;
	}

	return 1;
}

static void* _open_stream(const void* obj)
{
	if(NULL == obj) 
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	union {
		const void* mem;
		const rls_obj_t* obj;
		void*       ret;
	} conv = {
		.mem = obj
	};

	if(conv.obj->opened) 
		ERROR_PTR_RETURN_LOG("Try to dereference a CURL RLS twice");

	return conv.ret;
}

static int _free_obj(void* objmem)
{
	if(NULL == objmem)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	rls_obj_t* obj = (rls_obj_t*)objmem;

	if(!obj->curl_using) 
		return _obj_free_impl(obj);

	obj->committed = 0;

	return 0;
}

static int _close_stream(void* stream)
{
	(void)stream;
	return 0;
}

static void* _copy_obj(const void* obj)
{
	(void)obj;
	ERROR_PTR_RETURN_LOG("The operation is not supported");
}

static int _eos_stream(const void* stream)
{
	if(NULL == stream)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	const rls_obj_t* obj = (const rls_obj_t*)stream;

	/* If the libcurl is using this, it's definitely possible that the stream
	 * has more data */
	if(obj->curl_using)
		return 0;

	if(obj->pq_front_idx == obj->pq_rear_idx || (obj->pq_rear_idx - obj->pq_front_idx == 1 && obj->pq_rear_ofs == obj->pq_front_ofs))
		return 1;

	return 0;
}

static size_t _read_stream(void* __restrict stream, void* __restrict buffer, size_t count)
{
	size_t rc = 0;

	if(NULL == stream || NULL == buffer || ERROR_CODE(size_t) == count)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	rls_obj_t* obj = (rls_obj_t*)stream;

	/* We need to read the remaining data in the first page */
	size_t bytes_to_read = count;

	if(obj->pq_front_idx == obj->pq_rear_idx) 
		bytes_to_read = 0;
	else if(obj->pq_rear_idx - obj->pq_front_idx == 1 && count > obj->pq_rear_ofs - obj->pq_front_ofs)
	{
		/* In this case it's possible that obj->pq_rear_ofs is temporarily samller than obj->pq_front_ofs
		 * But when this happen, the entire page should be definitely readable. So it won't break the correctness
		 * of the code though */
		bytes_to_read = obj->pq_rear_ofs - obj->pq_front_ofs;
	}
	else if(count > _PAGESIZE - obj->pq_front_ofs) 
		bytes_to_read = _PAGESIZE - obj->pq_front_ofs;

	if(bytes_to_read > count) bytes_to_read = count;

	if(bytes_to_read > 0)
	{
		memcpy(buffer, (const char*)obj->data_pages[obj->pq_front_ofs & (RLS_PAGE_QUEUE_SIZE - 1)] + obj->pq_front_ofs, bytes_to_read);

		BARRIER();

		if(bytes_to_read + obj->pq_front_ofs >= _PAGESIZE) 
		{
			obj->pq_front_ofs = 0;
			BARRIER();
			obj->pq_front_idx ++;
		}

		rc += bytes_to_read;
	}

	count -= bytes_to_read;
	buffer = (char* __restrict)buffer + bytes_to_read;

	if(obj->curl_waiting) 
	{
		LOG_DEBUG("Notifying the curl handle for write buffer availalibity");
		/* TODO: client_notify_write_ready(rls->curl_handle); */
	}

	return rc;
}

scope_token_t rls_obj_commit(rls_obj_t* obj)
{
	if(NULL == obj) ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	if(obj->committed) 
		ERROR_RETURN_LOG(scope_token_t, "Cannot commit the same object twice");

	obj->committed = 1;
	
	scope_entity_t ent = {
		.data = obj,
		.open_func  = _open_stream,
		.close_func = _close_stream,
		.eos_func   = _eos_stream,
		.read_func  = _read_stream,
		.copy_func  = _copy_obj,
		.free_func  = _free_obj
	};

	scope_token_t ret;

	if(ERROR_CODE(scope_token_t) == (ret = pstd_scope_add(&ent)))
		obj->committed = 0;

	return ret;
}
