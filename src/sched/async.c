/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @note I have thought about if the async task queue should be lock-free and do we really care about the
 *       lock overhead of posting a async task to the queue. The answer seems not. <br/>
 *       For the enttire processing procedure, it's hard to believe that most of the time the worker thread 
 *       is initializing the async task - Most of the operation should be done by the worker thread, unless
 *       the slow operations. <br/>
 *       Although the global mutex will serialize all the worker threads if every worker is starting a async
 *       task, but this situation is almost impossible. This is because the assumption that the time for each
 *       request being processed, the async initiazation time is tiny. It's not likely that multiple worker thread 
 *       trying to start the async task at the same time.
 *       Which means the benefit we gain from lock-free desgin seems really small. <br/>
 *       On the other hand, even if this serialization happens, the time we post a message to the queue should be much
 *       faster than the actual IO or other slow operation. <br/>
 *       In addtion, without the lock-free desgin, the structure of the async task queue should be much simplier and 
 *       should have less chance to make mistake. <br/>
 *       So we need to use the mutex base, multiple writer, multiple reader, blocking queue desgin. Which means we have
 *       two condition variable, one used to block the writers, another used to block readers. 
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <error.h>

#include <utils/log.h>
#include <utils/thread.h>
#include <utils/mempool/objpool.h>

#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/equeue.h>

#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <sched/rscope.h>
#include <sched/service.h>
#include <sched/task.h>
#include <sched/async.h>

#include <lang/prop.h>

/**
 * @brief The number of the worker thread in the async task thread pool
 **/
static uint32_t _nthread = 32;

/**
 * @brief The maximum size of the queue
 **/
static uint32_t _queue_size = 65536;

/**
 * @brief The magic number used to verify a pointer is actually the async task handle
 **/
#define _HANDLE_MAGIC 0x35fc32ffu

/**
 * @brief The async task status
 **/
typedef enum {
	_STATE_INIT,   /*!< Indicates the task is about to call async_setup function */
	_STATE_EXEC,   /*!< The task is about to call async_exec function */
	_STATE_DONE    /*!< The task is about to call asnyc_clean function and emit the event */
} _state_t;

/**
 * @brief The task handle
 **/
typedef struct {
	uint32_t            magic_num;    /*!< The magic number we used to make sure the task handle valid */
	uint32_t            wait_mode:1;  /*!< If this task is in a wait mode */
	int                 status_code;  /*!< The status code of this task */
	_state_t            state;        /*!< The state of this handle */
	sched_loop_t*       sched_loop;   /*!< The scheduler loop */
	sched_task_t*       sched_task;   /*!< The scheduler task we are working on */
	runtime_task_t*     exec_task;    /*!< The async_exec task */
	runtime_task_t*     cleanup_task; /*!< The async_cleanup task */
} _handle_t;

/**
 * @brief The data structure for the async task queue
 * @note  The writer would only be blocked when the
 *        queue is full and the reader would only be blocked when the queue is empty. 
 *        Because the queue size should be strictly larger than 0. So reader and writer won't 
 *        be blocked at the same time. (It's impossible there are readers and writers being
 *        blocked at the same time). Which means we can use the condition variable
 *        for both reader and writer.
 **/
static struct {
	uint32_t             size;   /*!< The size of the queue */
	uint32_t             front;  /*!< The queue front */
	uint32_t             rear;   /*!< The queue rear */
	pthread_mutex_t      mutex;  /*!< The mutex used to block the readers */
	pthread_cond_t       cond;   /*!< The condvar used to block the either reader or writer */
	mempool_objpool_t*   pool;   /*!< The memory pool used to allocate handles */
	_handle_t**          data;   /*!< The actual queue data array */
} _queue;

/**
 * @brief The initiazation state
 **/
static int _init = 0;

/**
 * @brief setup the async task processor properties
 * @param symbol The symbol of the property
 * @param value The value to set
 * @param data The data
 * @return status code
 **/
static inline int _set_prop(const char* symbol, lang_prop_value_t value, const void* data)
{
	(void)data;
	if(strcmp(symbol, "nthread") == 0)
	{
		if(value.type != LANG_PROP_TYPE_INTEGER) ERROR_RETURN_LOG(int, "Type mismatch");
		_nthread = (uint32_t)value.num;
		LOG_DEBUG("Setting the number of async processing thread to %u", _nthread);
	}
	else if(strcmp(symbol, "queue_size") == 0)
	{
		if(value.type != LANG_PROP_TYPE_INTEGER) ERROR_RETURN_LOG(int, "Type mismatch");
		uint32_t desired = (uint32_t)value.num;
		uint32_t actual = 1;
		for(;desired > 1; desired >>= 1, actual <<= 1);
		if((uint32_t)value.num > actual) actual <<= 1;
		if((uint32_t)value.num != actual)
			LOG_WARNING("Adjusted the desired async processor queue size from %u to %u", (uint32_t)value.num, actual);
		LOG_DEBUG("Setting the async processor queue size to %u", actual);
		_queue_size = actual;
	}
	else 
	{
		LOG_WARNING("Invalid property scheduler.async.%s", symbol);
		return 0;
	}
	return 0;
}

int sched_async_init()
{
	(void)_queue;
	lang_prop_callback_t cb = {
		.param         = NULL,
		.get           = NULL,
		.set           = _set_prop,
		.symbol_prefix = "scheduler.async" 
	};

	return lang_prop_register_callback(&cb);
}

int sched_async_finalize()
{
	return 0;
}

int sched_async_start()
{
	if(NULL != _queue.data) 
		ERROR_RETURN_LOG(int, "Cannot initialize the async task queue twice");

	/* First thing, let's initialze the queue */
	_queue.size = _queue_size;
	_queue.front = _queue.rear = 0;

	if(pthread_mutex_init(&_queue.mutex, NULL) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot intialize the queue mutex");

	if(pthread_cond_init(&_queue.cond, NULL) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the queue cond variable");

	if(NULL == (_queue.data = (_handle_t**)malloc(sizeof(_queue.data[0]) * _queue.size)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the queue memory");

	if(NULL == (_queue.pool = mempool_objpool_new(sizeof(_handle_t))))
		ERROR_LOG_GOTO(ERR, "Cannot create memory pool for async handles");

	/* TODO: Then we need to start the async processing threads */


	/* Finally, we  should set the initialization flag */
	_init = 1;

	return 0;

ERR:

	if(_queue.data != NULL) free(_queue.data);
	if(_queue.pool != NULL) mempool_objpool_free(_queue.pool);

	return ERROR_CODE(int);
}

int sched_async_kill()
{
	if(!_init) ERROR_RETURN_LOG(int, "The async processor haven't been started yet");
	int rc = 0;

	/* TODO: stop all the started async processing threads */

	/* We need to dispose the queue */
	uint32_t i;
	for(i = _queue.front; i < _queue.rear; i ++)
	{
		if(_queue.data[i] == NULL) continue;
		if(_queue.data[i]->exec_task != NULL && ERROR_CODE(int) == runtime_task_free(_queue.data[i]->exec_task))
			rc = ERROR_CODE(int);
		if(ERROR_CODE(int) == mempool_objpool_dealloc(_queue.pool, _queue.data[i]))
			rc = ERROR_CODE(int);
	}

	if(ERROR_CODE(int) == mempool_objpool_free(_queue.pool))
		rc = ERROR_CODE(int);

	free(_queue.data);

	_queue.data = NULL;
	_queue.size = 0;
	_queue.front = _queue.rear = 0;

	_init = 0;

	return rc;
}

int sched_async_task_post(sched_loop_t* loop, sched_task_t* task)
{
	/* First, let's verify this task is a valid async init task */
	if(NULL == loop || NULL == task) 
		ERROR_RETURN_LOG(int, "Invalid argumetns");

	if(task->exec_task == NULL)
		ERROR_RETURN_LOG(int, "The async processor cannot take an uninstantiated task");

	if(RUNTIME_TASK_FLAG_GET_ACTION(task->exec_task->flags) != (RUNTIME_TASK_FLAG_ACTION_ASYNC | RUNTIME_TASK_FLAG_ACTION_INIT))
		ERROR_RETURN_LOG(int, "The async_setup task is expected");

	/* Then let's construct the handle */
	runtime_task_t *async_exec = NULL;
	runtime_task_t *async_cleanup = NULL;
	_handle_t* handle = mempool_objpool_alloc(_queue.pool);
	
	if(NULL == handle) 
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the handle");

	handle->magic_num   = _HANDLE_MAGIC;
	handle->wait_mode   = 0;
	handle->state       = _STATE_INIT;
	handle->sched_loop  = loop;
	handle->sched_task  = task;
	handle->status_code = 0;

	/* After that we need to call the async_setup function to get this initialized */
	if(ERROR_CODE(int) == runtime_task_start(task->exec_task, (runtime_api_async_handle_t*)handle))
		ERROR_LOG_GOTO(ERR, "The async setup task returns an error code");

	/* Ok it seems the task has been successfully setup, construct its continuation at this point */
	if(ERROR_CODE(int) == runtime_task_async_companions(task->exec_task, &async_exec, &async_cleanup))
		ERROR_LOG_GOTO(ERR, "Cannot Create the companion of the task");
	
	handle->state = _STATE_EXEC;

	if(ERROR_CODE(int) == runtime_task_free(task->exec_task))
		ERROR_LOG_GOTO(ERR, "Cannot dispose the async setup task");

	task->exec_task = async_cleanup;
	handle->exec_task = async_exec;

	/* At this point, we got a fully initialized async task handle */

	/* TODO: we need to add the task to the queue */


	return 0;
ERR:
	if(task->exec_task != NULL && task->exec_task != async_cleanup) 
		runtime_task_free(task->exec_task);

	if(async_exec != NULL) 
		runtime_task_free(async_exec);

	if(async_cleanup != NULL)
	{
		handle->status_code = ERROR_CODE(int);
		if(ERROR_CODE(int) == runtime_task_start(async_cleanup, (runtime_api_async_handle_t*)handle))
			LOG_WARNING("The async cleanup task returns an error code");
		runtime_task_free(async_cleanup);
	}

	if(NULL != handle) 
		mempool_objpool_dealloc(_queue.pool, handle);

	return ERROR_CODE_OT(int);
}
