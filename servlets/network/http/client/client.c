/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <curl/curl.h>

#include <pservlet.h>
#include <pstd.h>

#include <client.h>

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
 * @brief The client loop context, it may have multiple threads
 * @todo Currently we use epoll direcly for simplicity. 
 *       But we need to support other flavor of event driven APIs
 **/
typedef struct {
	/***** The global resources ********/
	uint32_t  tid;          /*!< The thread ID */
	int       epoll_fd;     /*!< The epoll fd */
	CURLM*    curlm;        /*!< The CURL Multi interface */
	_req_t*   req_buf;      /*!< The buffer for the request data */
	pthread_t thread;       /*!< Current pthread object */
	
	/******** The unused request buffer list ******/
	uint32_t  unused;       /*!< The unused request list */

	/******** The pending-for-add quee ************/
	uint32_t* add_queue;       /*!< The actual pending queue */
	uint32_t  add_queue_front; /*!< The front pointer of the add queue */
	uint32_t  add_queue_rear;  /*!< The rear pointer of the add queue */
	int       event_fd;        /*!< The event fd use for add notification */

	/******** The pending request heap ***********/
	uint32_t* request_heap;       /*!< The pending request heap */
	uint32_t  request_heap_size;  /*!< The pending request heap size */
} _thread_ctx_t;

/**
 * @brief The global context of the client library 
 **/
static struct {
	uint32_t        ref_cnt;         /*!< How many servlet is currently using this */
	uint32_t        queue_size;      /*!< The maximum number of the queue size for each thread */
	uint32_t        pr_limit;        /*!< The maximum number of parallel request for each thread */
	uint32_t        num_threads;     /*!< The maximum number of client threads */
	uint32_t        thread_cap;      /*!< The capacity of the thread context array */
	_thread_ctx_t*  thread_ctx;      /*!< The thread context objects */
} _ctx;

/**
 * @brief Cleanup all the client thread
 * @return status code
 **/
static inline int _thread_cleanup(void)
{
	uint32_t i;

	for(i = 0; i < _ctx.num_threads; i ++)
	{
		uint32_t j;
		for(j = 0; j < _ctx.queue_size; j ++)
		{
			/* TODO: Kill all the undergoing requests, dispose all the pending-to-add or queued requests */
		}

		/* TODO: Join the thread */

		if(NULL != _ctx.thread_ctx[i].req_buf) free(_ctx.thread_ctx[i].req_buf);
		if(NULL != _ctx.thread_ctx[i].add_queue) free(_ctx.thread_ctx[i].add_queue);
		if(NULL != _ctx.thread_ctx[i].request_heap) free(_ctx.thread_ctx[i].request_heap);

		if(NULL != _ctx.thread_ctx[i].curlm) curl_multi_cleanup(_ctx.thread_ctx[i].curlm);
		if(_ctx.thread_ctx[i].event_fd > 0) close(_ctx.thread_ctx[i].event_fd);
		if(_ctx.thread_ctx[i].epoll_fd > 0) close(_ctx.thread_ctx[i].epoll_fd);
	}

	return 0;
}

/**
 * @brief Initailize a new client thread
 * @note This function will initialize the epoll, eventfd and curl. 
 *       However, the size of the queue can be changed later. Thus
 *       it should be initialized whenever the thread starts to run
 * @return status code
 **/
static inline int _thread_init(void)
{
	_thread_ctx_t* buf = NULL;
	if(_ctx.thread_ctx == NULL)
		buf = _ctx.thread_ctx = (_thread_ctx_t*)malloc(sizeof(*_ctx.thread_ctx));
	else if(_ctx.num_threads == _ctx.thread_cap)
		buf = (_thread_ctx_t*)realloc(_ctx.thread_ctx, sizeof(*_ctx.thread_ctx) * (1 + _ctx.thread_cap));

	if(buf == NULL) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the new thread context");

	if(_ctx.num_threads == _ctx.thread_cap)
		_ctx.thread_cap ++;

	memset(_ctx.thread_ctx + _ctx.num_threads, 0, sizeof(_ctx.thread_ctx[0]));

	_thread_ctx_t* current = _ctx.thread_ctx + _ctx.num_threads;

	current->tid = _ctx.num_threads;

	if(NULL == (current->curlm = curl_multi_init()))
		ERROR_LOG_GOTO(ERR, "Cannot initialize the CURL Multi interface");

	if(-1 == (current->epoll_fd = epoll_create1(0)))
		ERROR_LOG_GOTO(ERR, "Cannot create epoll for the HTTP client thread");

	if(-1 == (current->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)))
		ERROR_LOG_GOTO(ERR, "Cannnot create event FD for the HTTP client thread");

	struct epoll_event event = {
		.events = EPOLLIN | EPOLLET,
		.data   = {
			.ptr = NULL
		}
	};

	if(-1 == epoll_ctl(current->epoll_fd, EPOLL_CTL_ADD, current->event_fd, &event))
		ERROR_LOG_GOTO(ERR, "Cannot add event FD to the HTTP client thread");

	return 0;
ERR:
	if(NULL != current->curlm)  curl_multi_cleanup(current->curlm);
	if(current->event_fd >= 0) close(current->event_fd);
	if(current->epoll_fd >= 0) close(current->epoll_fd);
	return ERROR_CODE(int);
}

int client_init(uint32_t queue_size, uint32_t parallel_limit, uint32_t num_threads)
{
	if(_ctx.ref_cnt == 0)
	{
		LOG_DEBUG("Initializing libcurl");
		CURLcode rc;
		if(0 != (rc = curl_global_init(CURL_GLOBAL_ALL)))
			ERROR_RETURN_LOG(int, "Cannnot initialize libcurl: %s", curl_easy_strerror(rc));
	}

	while(_ctx.num_threads < num_threads)
		if(ERROR_CODE(int) == _thread_init())
		{
			if(_ctx.ref_cnt == 0) curl_global_cleanup();
			ERROR_RETURN_LOG(int, "Cannot initialize the new thread");
		}

	if(_ctx.pr_limit < parallel_limit) _ctx.pr_limit = parallel_limit;

	for(;_ctx.queue_size < queue_size; _ctx.queue_size <<= 1);
	
	_ctx.ref_cnt ++;
	
	return 0;
}

int client_finalize(void)
{
	int ret = 0;
	if(_ctx.ref_cnt > 0 && --_ctx.ref_cnt == 0)
	{
		if(NULL != _ctx.thread_ctx)
		{
			if(ERROR_CODE(int) == _thread_cleanup())
			{
				ret = ERROR_CODE(int);
				LOG_ERROR("Cannot cleanup the thread local data");
			}

			free(_ctx.thread_ctx);
		}
	}

	return ret;
}
