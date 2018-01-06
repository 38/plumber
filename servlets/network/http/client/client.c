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
	uint32_t                    in_use:1;      /*!< indicates if this request buffer is being used */
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
	uint32_t  started:1;    /*!< Indicates if the client thread has been started */
	uint32_t  killed:1;     /*!< Indicates if this client has been killed */
	uint32_t  tid:30;       /*!< The thread ID */
	int       epoll_fd;     /*!< The epoll fd */
	CURLM*    curlm;        /*!< The CURL Multi interface */
	_req_t*   req_buf;      /*!< The buffer for the request data */
	pthread_t thread;       /*!< Current pthread object */
	
	/******** The unused request buffer list ******/
	uint32_t  unused;       /*!< The unused request list */

	/******** The pending-for-add quee ************/
	int       event_fd;        /*!< The event fd use for add notification */
	uint32_t  add_queue_front; /*!< The front pointer of the add queue */
	uint32_t  add_queue_rear;  /*!< The rear pointer of the add queue */
	uint32_t* add_queue;       /*!< The actual pending queue */

	/******** The pending request heap ***********/
	uint32_t* req_heap;            /*!< The pending request heap */
	uint32_t  req_heap_size;       /*!< The pending request heap size */
} _thread_ctx_t;

/**
 * @brief The global context of the client library 
 **/
static struct {
	uint32_t        ref_cnt;            /*!< How many servlet is currently using this */
	pthread_mutex_t mutex;              /*!< The pthread mutex */
	uint32_t        queue_size;         /*!< The maximum number of the queue size for each thread */
	uint32_t        pr_limit;           /*!< The maximum number of parallel request for each thread */
	uint32_t        num_threads;        /*!< The maximum number of client threads */
	uint32_t        thread_cap;         /*!< The capacity of the thread context array */
	uint32_t        num_started_threads;/*!< The number of started client threads */
	_thread_ctx_t*  thread_ctx;         /*!< The thread context objects */
} _ctx;

static void* _client_main(void* data)
{
	_thread_ctx_t* ctx = (_thread_ctx_t*)data;

	if(pthread_mutex_lock(&_ctx.mutex) != 0)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot acquire the global client lock");

	ctx->started = 1;
	_ctx.num_started_threads ++;

	if(pthread_mutex_unlock(&_ctx.mutex) != 0)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot release the global client lock");

	LOG_NOTICE("Client thread #%u is started", ctx->tid);

	while(!ctx->killed)
	{
		/* TODO: actual processing logic */
		sleep(1);
	}

	/* TODO: Dispose the existing requests */

	LOG_NOTICE("Client thread #%u is terminated", ctx->tid);

	return NULL;
}

/**
 * @brief Actually start the thread and allocate all the buffer and queues for this thread
 * @return status code 
 **/
static inline int _ensure_threads_started(void)
{
	int rc = 0;
	if(_ctx.num_threads != _ctx.num_started_threads)
	{
		if(pthread_mutex_lock(&_ctx.mutex) != 0)
			ERROR_RETURN_LOG(int, "Cannot lock the global client mutex");
		uint32_t i;
		for(i = 0; i < _ctx.num_threads; i ++)
		{
			if(!_ctx.thread_ctx[i].started)
			{
				LOG_NOTICE("Starting client thread #%u", i);

				if(NULL == (_ctx.thread_ctx[i].req_buf = (_req_t*)malloc(sizeof(_req_t) * _ctx.queue_size)))
					ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the request buffer for client thread #%u", i);

				uint32_t j;
				for(j = 0; j < _ctx.queue_size; j ++)
				{
					_ctx.thread_ctx[i].req_buf[j].in_use = 0;
					_ctx.thread_ctx[i].req_buf[j].next_unused = (j == _ctx.queue_size - 1) ? ERROR_CODE(uint32_t) : (j + 1);
				}

				_ctx.thread_ctx[i].unused = 0;

				if(NULL == (_ctx.thread_ctx[i].add_queue = (uint32_t*)malloc(sizeof(uint32_t) * _ctx.queue_size)))
					ERROR_LOG_ERRNO_GOTO(THREAD_ERR, "Cannot allocate memory for the add queue for client thread #%u", i);

				_ctx.thread_ctx[i].add_queue_front = 0;
				_ctx.thread_ctx[i].add_queue_rear  = 0;

				if(NULL == (_ctx.thread_ctx[i].req_heap = (uint32_t*)malloc(sizeof(uint32_t) * _ctx.queue_size)))
					ERROR_LOG_ERRNO_GOTO(THREAD_ERR, "Cannot allocate memory for the request priority queue for client thread #%u", i);
				_ctx.thread_ctx[i].req_heap_size = 0;

				if(pthread_create(&_ctx.thread_ctx[i].thread, NULL, _client_main, _ctx.thread_ctx + i) != 0)
					ERROR_LOG_ERRNO_GOTO(THREAD_ERR, "Cannot start the new client thread #%u", i);

				continue;
THREAD_ERR:
				if(NULL != _ctx.thread_ctx[i].req_buf) free(_ctx.thread_ctx[i].req_buf);
				if(NULL != _ctx.thread_ctx[i].add_queue) free(_ctx.thread_ctx[i].add_queue);
				if(NULL != _ctx.thread_ctx[i].req_heap) free(_ctx.thread_ctx[i].req_heap);

				_ctx.thread_ctx[i].req_buf = NULL;
				_ctx.thread_ctx[i].add_queue = NULL;
				_ctx.thread_ctx[i].req_heap = NULL;
				rc = ERROR_CODE(int);
				break;
			}
		}
		if(pthread_mutex_unlock(&_ctx.mutex) != 0)
			LOG_WARNING_ERRNO("Cannot reloase the global mutex lock");
	}

	return rc;
}

/**
 * @brief Cleanup all the client thread
 * @note The existing request cleanup should be responsible for the client thread itself, 
 *       this function only send the terminating message to the thread and wait the thread
 *       for completion
 * @return status code
 **/
static inline int _thread_cleanup(void)
{
	int rc = 0;
	uint32_t i;

	for(i = 0; i < _ctx.num_threads; i ++)
	{
		if(_ctx.thread_ctx[i].started)
		{
			_ctx.thread_ctx[i].killed = 1;

			uint64_t val = 1;
			int term_rc = 0;
			if(write(_ctx.thread_ctx[i].event_fd, &val, sizeof(val)) != sizeof(uint64_t))
			{
				LOG_ERROR_ERRNO("Cannot write event to the event FD for client thread #%u", i);
				term_rc = ERROR_CODE(int);
			}

			if(term_rc == 0 && pthread_join(_ctx.thread_ctx[i].thread, NULL) != 0)
			{
				LOG_ERROR_ERRNO("Cannot join the client thread #%u", i);
				term_rc = ERROR_CODE(int);
			}

			if(term_rc == ERROR_CODE(int)) rc = ERROR_CODE(int);
		}

		if(NULL != _ctx.thread_ctx[i].req_buf) free(_ctx.thread_ctx[i].req_buf);
		if(NULL != _ctx.thread_ctx[i].add_queue) free(_ctx.thread_ctx[i].add_queue);
		if(NULL != _ctx.thread_ctx[i].req_heap) free(_ctx.thread_ctx[i].req_heap);

		if(NULL != _ctx.thread_ctx[i].curlm) curl_multi_cleanup(_ctx.thread_ctx[i].curlm);
		if(_ctx.thread_ctx[i].event_fd > 0 && close(_ctx.thread_ctx[i].event_fd) < 0)
		{
			LOG_ERROR_ERRNO("Cannot close the event FD for the client thread #%u", i);
			rc = ERROR_CODE(int);
		}
		if(_ctx.thread_ctx[i].epoll_fd > 0 && close(_ctx.thread_ctx[i].epoll_fd) < 0)
		{
			LOG_ERROR_ERRNO("Cannot close the epoll FD for the client thread #%u", i);
			rc = ERROR_CODE(int);
		}
	}

	return rc;
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
	_thread_ctx_t* buf = _ctx.thread_ctx;

	if(_ctx.thread_ctx == NULL)
		buf = _ctx.thread_ctx = (_thread_ctx_t*)malloc(sizeof(*_ctx.thread_ctx));
	else if(_ctx.num_threads == _ctx.thread_cap)
		buf = (_thread_ctx_t*)realloc(_ctx.thread_ctx, sizeof(*_ctx.thread_ctx) * (1 + _ctx.thread_cap));

	if(buf == NULL) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the new thread context");

	_ctx.thread_ctx = buf;

	if(_ctx.num_threads == _ctx.thread_cap)
		_ctx.thread_cap ++;

	memset(_ctx.thread_ctx + _ctx.num_threads, 0, sizeof(*_ctx.thread_ctx));

	_thread_ctx_t* current = _ctx.thread_ctx + _ctx.num_threads;

	current->tid = (_ctx.num_threads & 0x3fffffffu);

	if(NULL == (current->curlm = curl_multi_init()))
		ERROR_LOG_GOTO(ERR, "Cannot initialize the CURL Multi interface");

	if(-1 == (current->epoll_fd = epoll_create1(0)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create epoll for the HTTP client thread");

	if(-1 == (current->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannnot create event FD for the HTTP client thread");

	struct epoll_event event = {
		.events = EPOLLIN | EPOLLET,
		.data   = {
			.ptr = NULL
		}
	};

	if(-1 == epoll_ctl(current->epoll_fd, EPOLL_CTL_ADD, current->event_fd, &event))
		ERROR_LOG_GOTO(ERR, "Cannot add event FD to the HTTP client thread");

	_ctx.num_threads ++;

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

		if(0 != pthread_mutex_init(&_ctx.mutex, NULL))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot intialize the global client mutex");

		_ctx.queue_size = 128;
		_ctx.pr_limit = 32;
		_ctx.num_threads = 0;
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
		curl_global_cleanup();
		
		if(NULL != _ctx.thread_ctx)
		{
			if(ERROR_CODE(int) == _thread_cleanup())
			{
				ret = ERROR_CODE(int);
				LOG_ERROR("Cannot cleanup the thread local data");
			}

			free(_ctx.thread_ctx);
		}

		if(0 != pthread_mutex_destroy(&_ctx.mutex))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot dispose the global client mutex");

	}

	return ret;
}

int client_add_request(const char* url, async_handle_t* handle, int priority, int block, client_request_setup_func_t setup, void* setup_data)
{
	(void) block;
	(void) setup;
	(void) setup_data;

	if(NULL == url || NULL == handle || priority < 0)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_threads_started())
		ERROR_RETURN_LOG(int, "Cannot ensure all the client threads initialized");

	/* TODO: actually post the request */

	return 0;
}
