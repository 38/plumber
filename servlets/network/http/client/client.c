/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <curl/curl.h>

#include <pservlet.h>
#include <pstd.h>

#include <client.h>

/**
 * @brief The initialization counter
 **/
static uint32_t _init_cnt = 0;

/**
 * @brief The data structure used to tracking a request
 **/
typedef struct {
	union {
		int                     priority;      /*!< The priority of this request */
		uint32_t                next_unused;   /*!< The next element unused request buffer list */
	};
	uint64_t                    serial_num;    /*!< The serial number for this request */
	const char*                 url;           /*!< The target URL */
	CURL*                       curl_handle;   /*!< The CURL hndle object, NULL if this object haven't been picked up */
	async_handle_t*             async_handle;  /*!< The servlet asynchronous handle, used for completion notification */
	client_request_setup_func_t setup_cb;      /*!< The setup callback */
	void*                       setup_data;    /*!< The data used by the setup callback */
} _req_t;

/**
 * @brief The client loop context
 * @todo Currently we use epoll direcly for simplicity. 
 *       But we need to support other flavor of event driven APIs
 **/
static struct {
	/***** The global resources ********/
	int       epoll_fd;     /*!< The epoll fd */
	CURLM*    curlm;        /*!< The CURL Multi interface */
	_req_t*   req_buf;      /*!< The buffer for the request data */
	uint32_t  queue_size;   /*!< The maximum number of the queue size */
	uint32_t  pr_limit;     /*!< The maximum number of parallel request */
	
	/******** The unused request buffer list ******/
	uint32_t  unused;       /*!< The unused request list */

	/******** The pending-for-add quee ************/
	uint32_t* add_queue;       /*!< The actual pending queue */
	uint32_t  add_queue_front; /*!< The front pointer of the add queue */
	uint32_t  add_queue_rear;  /*!< The rear pointer of the add queue */
	int       event_fd;        /*!< The event fd use for add notification */

	/******** The pending request heap ***********/
	uint32_t  request_heap;       /*!< The pending request heap */
	uint32_t  request_heap_size;  /*!< The pending request heap size */

} _ctx;

int client_servlet_init(void)
{
	if(_init_cnt == 0)
	{
		LOG_DEBUG("Initializing libcurl");
		CURLcode rc;
		if(0 != (rc = curl_global_init(CURL_GLOBAL_ALL)))
			ERROR_RETURN_LOG(int, "Cannnot initialize libcurl: %s", curl_easy_strerror(rc));

		if(NULL == (_ctx.curlm = curl_multi_init()))
			ERROR_LOG_GOTO(ERR, "Cannot iniailize the CURL Multi interface");

		LOG_DEBUG("Initialize epoll");
		if((_ctx.epoll_fd = epoll_create1(0)) == -1)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create epoll for HTTP client");

		LOG_DEBUG("Initialize event FD");
		if((_ctx.event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) == -1)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create event FD for HTTP client");
	}

	_init_cnt ++;

	return 0;
ERR:
	if(_ctx.epoll_fd > 0) close(_ctx.epoll_fd);
	if(_ctx.event_fd > 0) close(_ctx.event_fd);
	if(NULL != _ctx.curlm) curl_multi_cleanup(_ctx.curlm);
	curl_global_cleanup();
	return ERROR_CODE(int);
}

int client_servlet_finalize(void)
{
	int ret = 0;
	if(--_init_cnt == 0)
	{
		if(NULL != _ctx.curlm)
			curl_multi_cleanup(_ctx.curlm);

		LOG_DEBUG("Finalizing libcurl");
		curl_global_cleanup();
		
		if(_ctx.epoll_fd > 0 && close(_ctx.epoll_fd) == -1)
		{
			LOG_ERROR_ERRNO("Cannot close epoll fd");
			ret = ERROR_CODE(int);
		}

		if(_ctx.event_fd > 0 && close(_ctx.event_fd) == -1)
		{
			LOG_ERROR_ERRNO("Cannot cloese Event FD");
			ret = ERROR_CODE(int);
		}
	}

	return ret;
}
