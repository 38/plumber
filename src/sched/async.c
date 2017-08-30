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
#include <sys/time.h>

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
typedef struct _handle_t {
	uint32_t            magic_num;    /*!< The magic number we used to make sure the task handle valid */
	uint32_t            await_id;     /*!< The index of the handle in the wait list, ERROR_CODE if this handle is not in the awaiting list */
	int                 status_code;  /*!< The status code of this task */
	_state_t            state;        /*!< The state of this handle */
	sched_loop_t*       sched_loop;   /*!< The scheduler loop */
	sched_task_t*       sched_task;   /*!< The scheduler task we are working on */
	runtime_task_t*     exec_task;    /*!< The async_exec task */
	runtime_task_t*     cleanup_task; /*!< The async_cleanup task */
} _handle_t;

/**
 * @brief The data structure for a async thread
 **/
typedef struct {
	thread_t*    thread; /*!< The thread object for this async thread */
	_handle_t*   task;   /*!< The task this thread is current processing */
} _thread_data_t;

/**
 * @brief The data structure for the ATP(Async Task Processor)
 * @note  The writer would only be blocked when the
 *        queue is full and the reader would only be blocked when the queue is empty. 
 *        Because the queue size should be strictly larger than 0. So reader and writer won't 
 *        be blocked at the same time. (It's impossible there are readers and writers being
 *        blocked at the same time). Which means we can use the condition variable
 *        for both reader and writer.
 **/
static struct {
	uint32_t             init:1;    /*!< Indicates if the async task processor has been initialized */
	uint32_t             killed:1;  /*!< Indicates if the entire Async Task Processor has been killed */

	/*********** The queue related data ***************/
	uint32_t             q_cap;    /*!< The capacity of the queue */
	uint32_t             q_front;  /*!< The queue front */
	uint32_t             q_rear;   /*!< The queue rear */
	pthread_mutex_t      q_mutex;  /*!< The mutex used to block the readers */
	pthread_cond_t       q_cond;   /*!< The condvar used to block the either reader or writer */
	mempool_objpool_t*   q_pool;   /*!< The memory pool used to allocate handles */
	_handle_t**          q_data;   /*!< The actual queue data array */

	/********** The awaiting list *********************/
	uint32_t             al_cap;   /*!< The awaiting list capacity */
	uint32_t             al_size;  /*!< The actual number of awaiting tasks in the list */
	uint32_t**           al_unused;/*!< The unused wait list id */
	_handle_t**          al_list;  /*!< The awating list */

	/********* Thread releated data *******************/
	uint32_t             nthreads;    /*!< The number of threads in the async processing thread pool */
	_thread_data_t*      thread_data; /*!< The thread data for each async processing thread */
} _ctx;

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
	if(strcmp(symbol, "nthreads") == 0)
	{
		if(value.type != LANG_PROP_TYPE_INTEGER) ERROR_RETURN_LOG(int, "Type mismatch");
		_ctx.nthreads = (uint32_t)value.num;
		LOG_DEBUG("Setting the number of async processing thread to %u", _ctx.nthreads);
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
		_ctx.q_cap = actual;
	}
	else 
	{
		LOG_WARNING("Invalid property scheduler.async.%s", symbol);
		return 0;
	}
	return 0;
}

/**
 * @brief The main function of the async processor
 * @param data The thread data
 * @return status code
 **/
static void* _async_processor_main(void* data)
{
	_thread_data_t* thread_data = (_thread_data_t*)data;

	for(;!_ctx.killed;)
	{
		if(pthread_mutex_lock(&_ctx.q_mutex) < 0)
		{
			LOG_ERROR("Cannot acquire the async task queue mutex");
			continue;
		}

		struct timespec abstime;
		struct timeval now;
		gettimeofday(&now,NULL);
		abstime.tv_sec = now.tv_sec+1;
		abstime.tv_nsec = 0;

		while(_ctx.q_front == _ctx.q_rear && !_ctx.killed)
		{
			if(pthread_cond_timedwait(&_ctx.q_cond, &_ctx.q_mutex, &abstime) < 0 && errno != EINTR && errno != ETIMEDOUT)
				ERROR_LOG_ERRNO_GOTO(UNLOCK, "Cannot wait for the reader cond var");

			abstime.tv_sec ++;
		}

		if(_ctx.killed) goto UNLOCK;

		/* At this point we can claim the task */
		thread_data->task = _ctx.q_data[_ctx.q_front ++];

		/* At the same time, we need to check if this operation unblocks the writer */
		if(_ctx.q_rear - _ctx.q_front == _ctx.q_cap - 1 && pthread_cond_signal(&_ctx.q_cond) < 0)
			ERROR_LOG_ERRNO_GOTO(UNLOCK, "Cannot disp notify the writer");

UNLOCK:
		if(pthread_mutex_unlock(&_ctx.q_mutex) < 0)
			LOG_ERROR("Cannot release the async task queue mutex");

		/* Then we need check if we have picked up a task, if not, we need to wait for another one */
		if(thread_data->task == NULL) continue;

		/* TODO: At this point, we will be able to process it */
		/* At this point, the task should be the exec task */
	}

	return thread_data;
}

int sched_async_init()
{
	_ctx.q_cap = 65536;
	_ctx.nthreads = 32;
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
	uint32_t i = 0;
	if(_ctx.init) 
		ERROR_RETURN_LOG(int, "Cannot initialize the async task queue twice");

	/* First thing, let's initialze the queue */
	_ctx.q_front = _ctx.q_rear = 0;

	if(pthread_mutex_init(&_ctx.q_mutex, NULL) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot intialize the queue mutex");

	if(pthread_cond_init(&_ctx.q_cond, NULL) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the queue cond variable");

	if(NULL == (_ctx.q_data = (_handle_t**)malloc(sizeof(_ctx.q_data[0]) * _ctx.q_cap)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the queue memory");

	if(NULL == (_ctx.q_pool = mempool_objpool_new(sizeof(_handle_t))))
		ERROR_LOG_GOTO(ERR, "Cannot create memory pool for async handles");

	/* Then, we need to start the async processing threads */
	if(NULL == (_ctx.thread_data = (_thread_data_t*)calloc(sizeof(_thread_data_t), _ctx.nthreads)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the async task thread pool");


	for(i = 0; i < _ctx.nthreads; i ++)
		if(NULL == (_ctx.thread_data[i].thread = thread_new(_async_processor_main, _ctx.thread_data + i, THREAD_TYPE_ASYNC)))
			ERROR_LOG_GOTO(ERR, "Cannot start the new thread for the Async Task Processor");
	/* Finally, we  should set the initialization flag */
	_ctx.init = 1;

	return 0;

ERR:

	if(_ctx.q_data != NULL) free(_ctx.q_data);
	if(_ctx.q_pool != NULL) mempool_objpool_free(_ctx.q_pool);
	if(_ctx.thread_data != NULL)
	{
		_ctx.killed = 1;

		/* We need to let everyone know they are killed */
		pthread_cond_broadcast(&_ctx.q_cond);

		for(i = 0; i < _ctx.nthreads; i ++)
			if(_ctx.thread_data[i].thread != NULL)
				thread_free(_ctx.thread_data[i].thread, NULL);

		free(_ctx.thread_data);
	}
	return ERROR_CODE(int);
}

int sched_async_kill()
{
	if(!_ctx.init) ERROR_RETURN_LOG(int, "The async processor haven't been started yet");
	int rc = 0;

	_ctx.killed = 1;

	/* Let't kill all the async processing thread at this point */
	pthread_cond_broadcast(&_ctx.q_cond);
	uint32_t i;
	for(i = 0; i < _ctx.nthreads; i ++)
		if(ERROR_CODE(int) == thread_free(_ctx.thread_data[i].thread, NULL))
			rc = ERROR_CODE(int);
	free(_ctx.thread_data);

	/* We need to dispose the queue */
	for(i = _ctx.q_front; i < _ctx.q_rear; i ++)
	{
		if(_ctx.q_data[i & (_ctx.q_cap - 1)] == NULL) continue;
		if(_ctx.q_data[i & (_ctx.q_cap - 1)]->exec_task != NULL && 
		   ERROR_CODE(int) == runtime_task_free(_ctx.q_data[i & (_ctx.q_cap - 1)]->exec_task))
			rc = ERROR_CODE(int);
		if(ERROR_CODE(int) == mempool_objpool_dealloc(_ctx.q_pool, _ctx.q_data[i & (_ctx.q_cap - 1)]))
			rc = ERROR_CODE(int);
	}

	if(ERROR_CODE(int) == mempool_objpool_free(_ctx.q_pool))
		rc = ERROR_CODE(int);

	free(_ctx.q_data);

	_ctx.q_data = NULL;
	_ctx.q_front = _ctx.q_rear = 0;

	_ctx.init = 0;

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
	_handle_t* handle = mempool_objpool_alloc(_ctx.q_pool);
	
	if(NULL == handle) 
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the handle");

	handle->magic_num   = _HANDLE_MAGIC;
	handle->await_id    = ERROR_CODE(uint32_t);
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

	if(pthread_mutex_lock(&_ctx.q_mutex) < 0)
		ERROR_RETURN_LOG(int, "Cannot acquire the queue mutex");

	struct timespec abstime;
	struct timeval now;
	gettimeofday(&now,NULL);
	abstime.tv_sec = now.tv_sec+1;
	abstime.tv_nsec = 0;

	while(_ctx.q_rear - _ctx.q_front >= _ctx.q_cap && !_ctx.killed)
	{
		if(pthread_cond_timedwait(&_ctx.q_cond, &_ctx.q_mutex, &abstime) < 0 && errno != ETIMEDOUT && errno != EINTR)
			ERROR_RETURN_LOG(int, "Cannot wait for the writer condition variable");
		abstime.tv_sec ++;
	}
	
	if(_ctx.killed) 
	{
		LOG_INFO("The async process has been killed!");
		goto RET;
	}

	/* Then we need actually put the handle in the queue */
	_ctx.q_data[(_ctx.q_rear ++) & (_ctx.q_cap - 1)] = handle;

	/* If this queue is previously empty, then we need to notify the reader */
	if(_ctx.q_rear - _ctx.q_front == 1 && pthread_cond_signal(&_ctx.q_cond) < 0)
		ERROR_RETURN_LOG(int, "Cannot notify the reader about the incoming task");

RET:
	if(pthread_mutex_unlock(&_ctx.q_mutex) < 0)
		ERROR_RETURN_LOG(int, "Cannot reliease the queue mutex");

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
		mempool_objpool_dealloc(_ctx.q_pool, handle);

	return ERROR_CODE_OT(int);
}
