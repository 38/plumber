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
	volatile uint32_t commited:1;                       /*!< If this object has been commited to RLS */
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

	obj->commited = 0;
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

	if(from_curl && obj->commited)
	{
		/* If the object is already in RLS, just reset the flag */
		obj->curl_using = 0;
		return 0;
	}

	if(obj->commited) ERROR_RETURN_LOG(int, "Trying to dispose a committed RLS object");

	if(!from_curl && obj->curl_using) ERROR_RETURN_LOG(int, "Trying to dispose a RLS object using by CURL");

	return _obj_free_impl(obj);
}

int rls_obj_write(rls_obj_t* obj, const void* data, size_t count)
{
	if(NULL == obj || NULL == data)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t n_free_pages = (obj->pq_rear_idx - obj->pq_front_idx) - RLS_PAGE_QUEUE_SIZE;
	uint32_t n_free_bytes = (obj->pq_rear_idx != obj->pq_front_idx) ? _PAGESIZE - obj->pq_rear_ofs : 0;

	if(n_free_pages & 0x80000000) n_free_pages = 0;

	if(n_free_pages * _PAGESIZE + n_free_bytes < count)
	{
		LOG_DEBUG("The write buffer is full, stop recieving data");
		return 0;
	}

	if(n_free_bytes > 0)
	{
		LOG_DEBUG("Current page buffer has %u unused bytes, try to use it first", n_free_bytes);

		uint32_t bytes_to_copy = n_free_bytes;

		if(bytes_to_copy > count) bytes_to_copy = (uint32_t)count;

		memcpy((char*)obj->data_pages[obj->pq_rear_idx] + obj->pq_rear_ofs, data, bytes_to_copy);

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

		memcpy(obj->data_pages[obj->pq_rear_idx], data, _PAGESIZE);

		count -= _PAGESIZE;
		data = (const char*)data + _PAGESIZE;

		BARRIER();

		obj->pq_rear_idx ++;
	}

	if(count > 0)
	{
		LOG_DEBUG("Write data to the new page");

		memcpy(obj->data_pages[obj->pq_rear_idx], data, count);

		BARRIER();

		obj->pq_rear_ofs = (uint32_t)count;
	}

	return 1;
}
