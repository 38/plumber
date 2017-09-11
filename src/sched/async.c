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
 * @brief The magic number used for a fake handle, this is used when we testing the async servlet
 **/
#define _FAKE_HANDLE_MAGIC 0x42c5ff3cu

/**
 * @brief The async task status
 **/
typedef enum {
	_STATE_INIT,     /*!< Indicates the task is about to call async_setup function */
	_STATE_EXEC,     /*!< The task is about to call async_exec function */
	_STATE_DONE,     /*!< The task is about to call asnyc_clean function and emit the event */
	_STATE_AWAITING, /*!< The task is waitnig for the async_cntl call */
	_STATE_CANCELED  /*!< The task is canceled, which means we do not run the async_exec */
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
} _handle_t;

/**
 * @brief The fake handle
 **/
typedef struct {
	uint32_t    magic_num;     /*!< The magic number */
	uint32_t    completed:1;  /*!< Indicates if this task has been completed */
} _fake_handle_t;;
STATIC_ASSERTION_TYPE_COMPATIBLE(_handle_t, magic_num, _fake_handle_t, magic_num);

/**
 * @brief The data structure for a async thread
 **/
typedef struct {
	thread_t*    thread; /*!< The thread object for this async thread */
	_handle_t*   task;   /*!< The task this thread is current processing */
} _thread_data_t;

/**
 * @brief The data structure used to describe an awaiting task
 **/
typedef struct {
	_handle_t*    task;        /*!< The asnyc task handle that is awaiting for the completed signal */
	uint32_t      valid;       /*!< Indicates if this awaiter is current valid */
	uint32_t      next;        /*!< The next awaiting task which is already compelted */
} _awaiter_t;

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
	uint32_t             q_mutex_init:1;  /*!< If queue mutex has been initalized */
	uint32_t             al_mutex_init:1; /*!< If the waiting list mutex has been initialized */
	uint32_t             q_cond_init:1;   /*!< If the queue condvar has been initialized */

	/*********** The queue related data ***************/
	uint32_t             q_cap;    /*!< The capacity of the queue */
	uint32_t             q_front;  /*!< The queue front */
	uint32_t             q_rear;   /*!< The queue rear */
	pthread_mutex_t      q_mutex;  /*!< The mutex used to block the readers */
	pthread_cond_t       q_cond;   /*!< The condvar used to block the either reader or writer */
	mempool_objpool_t*   q_pool;   /*!< The memory pool used to allocate handles */
	_handle_t**          q_data;   /*!< The actual queue data array */

	/********** The awaiting list *********************/
	uint32_t             al_cap;    /*!< The awaiting list capacity */
	pthread_mutex_t      al_mutex;  /*!< The mutex used for the awaiting list */
	uint32_t             al_unused; /*!< The head of the unused awaiting list slot */
	uint32_t             al_done;   /*!< The head of the completed awaiting task list */
	_awaiter_t*          al_list;   /*!< The awaiting list */

	/********* Thread releated data *******************/
	uint32_t             nthreads;      /*!< The number of threads in the async processing thread pool */
	_thread_data_t*      thread_data;   /*!< The thread data for each async processing thread */
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
	else if(strcmp(symbol, "wait_list_size") == 0)
	{
		if(value.type != LANG_PROP_TYPE_INTEGER) ERROR_RETURN_LOG(int, "Type mismatch");
		_ctx.al_cap = (uint32_t)value.num;
	}
	else
	{
		LOG_WARNING("Invalid property scheduler.async.%s", symbol);
		return 0;
	}

	return 1;
}

/**
 * @brief post a task completion event to the event qeueue
 * @param token The event queue token we used to post the task
 * @param handle The handle we want to pose
 * @return status code
 **/
static inline int _post_task_complete_event(itc_equeue_token_t token, _handle_t* handle)
{
	LOG_DEBUG("The task is not in the wait mode, sending the task compelted event to the event queue");
	itc_equeue_event_t event;

	event.type = ITC_EQUEUE_EVENT_TYPE_TASK;
	event.task.loop = handle->sched_loop;
	event.task.task = handle->sched_task;
	event.task.async_handle = (runtime_api_async_handle_t*)handle;

	/* Send a event to the event queue, so that the scheduler knows about the event */
	if(ERROR_CODE(int) == itc_equeue_put(token, event))
	{
		/* At this point, we directly dispose the handle */
		if(handle->await_id != ERROR_CODE(uint32_t))
		{
			/* This means we need to remove it from the waiting list, becuase this
			 * is really rare, so we just go through the linked list */
			uint32_t prev = handle->state == _STATE_AWAITING ? ERROR_CODE(uint32_t) : _ctx.al_done;
			/* If this is the first element in the done list, we need to update the al_done pointer*/
			if(prev == handle->await_id)
			{
				_ctx.al_done = _ctx.al_list[prev].next;
				prev = ERROR_CODE(uint32_t);
			}
			/* Search for the prevoius node */
			for(;prev != ERROR_CODE(uint32_t) && _ctx.al_list[prev].next == handle->await_id; prev = _ctx.al_list[prev].next);
			if(prev != ERROR_CODE(uint32_t))
			    _ctx.al_list[prev].next = _ctx.al_list[handle->await_id].next;
			/* Insert the awaiter to the unused list */
			_ctx.al_list[handle->await_id].next = _ctx.al_unused;
			_ctx.al_unused = handle->await_id;
		}

		/*TODO: we could have an issue that the pending task in schedulder never get a chance to dispose the cerepsonding scheduler task,
		 *      Currently, we just leave it there, because this is not likely to happen, and when this happens, it's more likely the entire
		 *      system stop working from here.
		 *      So we don't address this issue here, but we definitely needs to handle that
		 **/

		/* Finally we need to dispose it */
		mempool_objpool_dealloc(_ctx.q_pool, handle);

		/* Of cousrse, we may have memory leak at this point, but it seems
		 * we can't do anything better than that, because the data flow is
		 * broken, and all the mechanism is not working here */
		LOG_ERROR("Cannot send the task event to the event queue");
		return ERROR_CODE(int);
	}

	return 0;
}

/**
 * @brief send the task completion event to the event qeueu
 * @param token The event queue token we want to use
 * @param set_error indicates if we need to set the status to error
 * @return status code
 **/
static inline int _notify_compeleted_awaiters(itc_equeue_token_t token, int set_error)
{
	int rc = 0;
	/* the first thing we need to do is to clean the awaiting list */
	if(pthread_mutex_lock(&_ctx.al_mutex) < 0)
	    ERROR_RETURN_LOG(int, "Cannot acquire the async task awaiting list mutex");
	else
	{
		/* Here we need to send all the compelted awaiting tasks to the event queue */
		uint32_t ptr;
		for(ptr = _ctx.al_done; ptr != ERROR_CODE(uint32_t);)
		{
			uint32_t cur = ptr;
			ptr = _ctx.al_list[ptr].next;
			_awaiter_t* this = _ctx.al_list + cur;

			if(set_error) this->task->status_code = ERROR_CODE(int);

			if(ERROR_CODE(int) == _post_task_complete_event(token, this->task))
			{
				LOG_ERROR("Cannot post the task compeletion event to the event queue");
				rc = ERROR_CODE(int);
			}

			/* Since we have passed the task handle to the event queue, so we should not dispose it
			 * The worst case of this is the event queue can not accept the handle, but in this case
			 * the _post_task_complete_event function will be responsible for disposing the handle */
			this->valid = 0;
			this->task = NULL;

			this->next = _ctx.al_unused;
			_ctx.al_unused = cur;
			_ctx.al_done = ptr;
		}
		if(pthread_mutex_unlock(&_ctx.al_mutex) < 0)
		    ERROR_RETURN_LOG(int, "Cannot release the async task awaiting list mutex");
	}

	return rc;
}

/**
 * @brief The main function of the async processor
 * @param data The thread data
 * @return status code
 **/
static void* _async_processor_main(void* data)
{
	thread_set_name("PBAsyncTask");

	_thread_data_t* thread_data = (_thread_data_t*)data;

	itc_equeue_token_t token = itc_equeue_module_token(ITC_MODULE_EVENT_QUEUE_SIZE);

	if(ERROR_CODE(itc_equeue_token_t) == token)
	    ERROR_PTR_RETURN_LOG("Cannot get the enent queue token for the async processing thread");

	for(;!_ctx.killed;)
	{
		/* We need to reset the previous task at this point, and the task should be able find by the scheudler thread
		 * with the completion event. However, if the task failed to post to the event queue, then it's the point to
		 * dispose it  */
		thread_data->task = NULL;

		/* Before we actually move ahead, we need to look at the awaiter list and make sure
		 * all the compeleted awaiters has been notified at this time */
		if(ERROR_CODE(int) == _notify_compeleted_awaiters(token, 0))
		    LOG_ERROR("Cannot notify the completed awaiters");

		/* Then we need to make sure that we have work to do */
		if(pthread_mutex_lock(&_ctx.q_mutex) < 0)
		    ERROR_PTR_RETURN_LOG("Cannot acquire the async task queue mutex");

		struct timespec abstime;
		struct timeval now;
		gettimeofday(&now,NULL);
		abstime.tv_sec = now.tv_sec+1;
		abstime.tv_nsec = 0;

		while(_ctx.q_front == _ctx.q_rear && !_ctx.killed)
		{
			if(pthread_cond_timedwait(&_ctx.q_cond, &_ctx.q_mutex, &abstime) < 0 && errno != EINTR && errno != ETIMEDOUT)
			    ERROR_LOG_ERRNO_GOTO(UNLOCK, "Cannot wait for the reader cond var");

			/* Because it's possible that all the async processing thread is being blocked here, so we need to have a way
			 * to make it work */
			if(ERROR_CODE(int) == _notify_compeleted_awaiters(token, 0))
			    LOG_ERROR("Cannot notify the completed awaiters");

			abstime.tv_sec ++;
		}

		if(_ctx.killed) goto UNLOCK;

		/* At this point we can claim the task */
		thread_data->task = _ctx.q_data[(_ctx.q_front ++) & (_ctx.q_cap - 1)];

		/* At the same time, we need to check if this operation unblocks the writer */
		if(_ctx.q_rear - _ctx.q_front == _ctx.q_cap - 1 && pthread_cond_signal(&_ctx.q_cond) < 0)
		    ERROR_LOG_ERRNO_GOTO(UNLOCK, "Cannot disp notify the writer");

UNLOCK:
		if(pthread_mutex_unlock(&_ctx.q_mutex) < 0)
		    LOG_ERROR("Cannot release the async task queue mutex");

		/* Then we need check if we have picked up a task, if not, we need to wait for another one */
		if(thread_data->task == NULL) continue;

		LOG_DEBUG("Staring the async exec task");
		if(ERROR_CODE(int) == runtime_task_start(thread_data->task->exec_task))
		{
			thread_data->task->status_code = ERROR_CODE(int);;
			LOG_ERROR("The async exec task returns an error");
		}
		else thread_data->task->status_code = 0;

		if(thread_data->task->await_id == ERROR_CODE(uint32_t))
		    thread_data->task->state = _STATE_DONE;

		/* Finally we can dispose the task at this point */
		if(ERROR_CODE(int) == runtime_task_free(thread_data->task->exec_task))
		{
			LOG_ERROR("Cannot dispose the executed async exec task");
			thread_data->task->status_code = ERROR_CODE(int);
		}

		thread_data->task->exec_task = NULL;

		if(thread_data->task->await_id == ERROR_CODE(uint32_t))
		{
			LOG_DEBUG("The task is completed and post the task completion event");
			if(ERROR_CODE(int) == _post_task_complete_event(token, thread_data->task))
			    LOG_ERROR("Cannot post the task completion event to the event queue");
		}
		else
		    LOG_DEBUG("The task is not done yet, waiting for the async_cntl call");
	}

	/* Before we actually stop running, we need to make sure all the pending task has been handled propertly */
	if(ERROR_CODE(int) == _notify_compeleted_awaiters(token, 1))
	    ERROR_PTR_RETURN_LOG("Cannot notify the completed awaiters");

	LOG_DEBUG("Async processing thread %p has been killed", thread_data);

	return thread_data;
}

int sched_async_init()
{
	_ctx.q_cap = 65536;
	_ctx.nthreads = 32;
	_ctx.al_cap = 65536;
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

	_ctx.q_mutex_init = 1;

	if(pthread_cond_init(&_ctx.q_cond, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the queue cond variable");

	_ctx.q_cond_init = 1;

	if(pthread_mutex_init(&_ctx.al_mutex, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the awaiting list mutex");

	_ctx.al_mutex_init = 1;

	if(NULL == (_ctx.q_data = (_handle_t**)malloc(sizeof(_ctx.q_data[0]) * _ctx.q_cap)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the queue memory");

	if(NULL == (_ctx.q_pool = mempool_objpool_new(sizeof(_handle_t))))
	    ERROR_LOG_GOTO(ERR, "Cannot create memory pool for async handles");

	/* Initialize the awaiting list */
	if(NULL == (_ctx.al_list = (_awaiter_t*)malloc(sizeof(_awaiter_t) * _ctx.al_cap)))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the awaiting task list");

	_ctx.al_unused = 0;

	for(i = 0; i < _ctx.al_cap; i ++)
	{
		_ctx.al_list[i].valid = 0;
		_ctx.al_list[i].next = i + 1;
		_ctx.al_list[i].task = NULL;
	}
	_ctx.al_list[_ctx.al_cap - 1].next = ERROR_CODE(uint32_t);
	_ctx.al_done = ERROR_CODE(uint32_t);

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
	if(_ctx.al_list != NULL) free(_ctx.al_list);

	if(_ctx.q_mutex_init)
	    pthread_mutex_destroy(&_ctx.q_mutex);

	if(_ctx.q_cond_init)
	    pthread_cond_destroy(&_ctx.q_cond);

	if(_ctx.al_mutex_init)
	    pthread_mutex_destroy(&_ctx.al_mutex);

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


	/* At this point we should take care of all the awating tasks */
	for(i = 0; i < _ctx.al_cap; i ++)
	    if(_ctx.al_list[i].valid && _ctx.al_list[i].task->state == _STATE_AWAITING)
	    {
		    if(ERROR_CODE(int) == mempool_objpool_dealloc(_ctx.q_pool, _ctx.al_list[i].task))
		        rc = ERROR_CODE(int);
	    }

	free(_ctx.al_list);
	free(_ctx.q_data);

	if(ERROR_CODE(int) == mempool_objpool_free(_ctx.q_pool))
	    rc = ERROR_CODE(int);

	_ctx.q_data = NULL;
	_ctx.q_front = _ctx.q_rear = 0;

	if(_ctx.q_mutex_init && pthread_mutex_destroy(&_ctx.q_mutex) < 0)
	{
		LOG_ERROR_ERRNO("Cannot destory the queue mutex");
		rc = ERROR_CODE(int);
	}
	else _ctx.q_mutex_init = 0;

	if(_ctx.al_mutex_init && pthread_mutex_destroy(&_ctx.al_mutex) < 0)
	{
		LOG_ERROR_ERRNO("Cannot destory the waiting list mutex");
		rc = ERROR_CODE(int);
	}
	else _ctx.al_mutex_init = 0;

	if(_ctx.q_cond_init && pthread_cond_destroy(&_ctx.q_cond) < 0)
	{
		LOG_ERROR_ERRNO("Cannot destory the queue cond var");
		rc = ERROR_CODE(int);
	}
	else _ctx.q_cond_init = 0;

	_ctx.init = 0;

	return rc;
}

int sched_async_task_post(sched_loop_t* loop, sched_task_t* task)
{
	int normal_rc = 1;
	int error_code = ERROR_CODE(int);
	/* First, let's verify this task is a valid async init task */
	if(NULL == loop || NULL == task)
	    ERROR_RETURN_LOG(int, "Invalid argumetns");

	if(task->exec_task == NULL)
	    ERROR_RETURN_LOG(int, "The async processor cannot take an uninstantiated task");

	if(RUNTIME_TASK_FLAG_GET_ACTION(task->exec_task->flags) != RUNTIME_TASK_FLAG_ACTION_INIT || !runtime_task_is_async(task->exec_task))
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

	task->exec_task->async_handle = (runtime_api_async_handle_t*)handle;

	/* After that we need to call the async_setup function to get this initialized */
#ifdef FULL_OPTIMIZATION
	if(ERROR_CODE(int) == runtime_task_start_async_setup_fast(task->exec_task))
#else
	if(ERROR_CODE(int) == runtime_task_start(task->exec_task))
#endif
	    ERROR_LOG_GOTO(ERR, "The async setup task returns an error code");

	/* Ok it seems the task has been successfully setup, construct its continuation at this point */
	if(ERROR_CODE(int) == runtime_task_async_companions(task->exec_task, &async_exec, &async_cleanup))
	    ERROR_LOG_GOTO(ERR, "Cannot Create the companion of the task");

	if(ERROR_CODE(int) == runtime_task_free(task->exec_task))
	    ERROR_LOG_GOTO(ERR, "Cannot dispose the async setup task");

	task->exec_task = async_cleanup;
	handle->exec_task = async_exec;

	if(handle->state == _STATE_CANCELED)
	{
		LOG_DEBUG("Not going to post the task to the async processor, because it has been canceled");
		if(ERROR_CODE(int) == runtime_task_free(handle->exec_task))
		    ERROR_LOG_GOTO(ERR, "Cannot dispose the async exec task");
		handle->exec_task = NULL;
		async_exec = NULL;
		/* At this point, the handle could be owned by the cleanup task already, so it's ok to return directly */
		return 0;
	}

	handle->state = _STATE_EXEC;


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
		if(async_exec != NULL && ERROR_CODE(int) == runtime_task_free(async_exec))
		    normal_rc = ERROR_CODE(int);
		if(ERROR_CODE(int) == mempool_objpool_dealloc(_ctx.q_pool, handle))
		    normal_rc = ERROR_CODE(int);
		/* What should be keep is the cleanup task, and it should be disposed by the scheduler */
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

	return normal_rc;
ERR:
	if(task->exec_task != NULL && task->exec_task != async_cleanup)
	{
		if(ERROR_CODE(int) != runtime_task_free(task->exec_task))
		    error_code = ERROR_CODE_OT(int);
	}

	if(task->exec_task == NULL) error_code = ERROR_CODE_OT(int);

	if(async_exec != NULL)
	    runtime_task_free(async_exec);

	if(async_cleanup != NULL)
	{
		handle->status_code = ERROR_CODE(int);
		if(ERROR_CODE(int) == runtime_task_start(async_cleanup))
		    LOG_WARNING("The async cleanup task returns an error code");

		if(ERROR_CODE(int) != runtime_task_free(async_cleanup))
		    error_code = ERROR_CODE_OT(int);
	}

	if(NULL != handle)
	    mempool_objpool_dealloc(_ctx.q_pool, handle);

	return error_code;
}

int sched_async_handle_dispose(runtime_api_async_handle_t* handle)
{
	_handle_t* mem = (_handle_t*)handle;

	if(NULL != mem && mem->magic_num == _FAKE_HANDLE_MAGIC)
	    return sched_async_fake_handle_free(handle);

	if(NULL == mem || mem->magic_num != _HANDLE_MAGIC)
	    ERROR_RETURN_LOG(int, "Invalid arguments: Invalid async task handle");

	return mempool_objpool_dealloc(_ctx.q_pool, mem);
}

int sched_async_handle_set_await(runtime_api_async_handle_t* handle)
{
	int rc = ERROR_CODE(int);
	_handle_t* task = (_handle_t*)handle;

	if(NULL == task || task->magic_num != _HANDLE_MAGIC)
	    ERROR_RETURN_LOG(int, "Invalid arguments: Invalid aync task handle");

	if(task->await_id != ERROR_CODE(uint32_t))
	    ERROR_RETURN_LOG(int, "Cannot set the task to wait mode twice");

	if(pthread_mutex_lock(&_ctx.al_mutex) < 0)
	    ERROR_RETURN_LOG(int, "Cannot acquire the async task waiting list mutex");

	if(ERROR_CODE(uint32_t) == _ctx.al_unused)
	    ERROR_LOG_GOTO(EXIT, "Too many tasks in the waiting list, scheduler.async.wait_list_size may be too small");

	uint32_t claimed = _ctx.al_unused;
	_ctx.al_unused = _ctx.al_list[claimed].next;

	_ctx.al_list[claimed].task = task;
	_ctx.al_list[claimed].next = ERROR_CODE(uint32_t);
	_ctx.al_list[claimed].valid = 1;
	task->await_id = claimed;
	task->state = _STATE_AWAITING;

	LOG_DEBUG("The task has been added to the waiting list as waiting task #%u", claimed);

	rc = 0;

EXIT:
	if(pthread_mutex_unlock(&_ctx.al_mutex) < 0)
	    ERROR_RETURN_LOG(int, "Cannot release the async task waiting list mutex");

	return rc;
}

int sched_async_handle_await_complete(runtime_api_async_handle_t* handle, int status)
{
	int rc = ERROR_CODE(int);
	_handle_t *task = (_handle_t*)handle;

	if(NULL == task || task->magic_num != _HANDLE_MAGIC)
	    ERROR_RETURN_LOG(int, "Invalid arguments: Invalid aync task handle");

	if(task->await_id == ERROR_CODE(uint32_t))
	    ERROR_RETURN_LOG(int, "Invalid arguments: The async task is not in the wait mode");

	if(pthread_mutex_lock(&_ctx.al_mutex) < 0)
	    ERROR_RETURN_LOG(int, "Cannot acquire the async task waiting list mutex");

	uint32_t slot = task->await_id;

	if(_ctx.al_list[slot].next != ERROR_CODE(uint32_t))
	{
		LOG_DEBUG("The task has already been notified");
		rc = 0;
		goto EXIT;
	}

	_ctx.al_list[slot].next = _ctx.al_done;
	_ctx.al_done = slot;
	task->state = _STATE_DONE;

	if(ERROR_CODE(int) == status && task->status_code != ERROR_CODE(int))
	{
		LOG_DEBUG("Setting the task status to failure");
		task->status_code = ERROR_CODE(int);
	}

	rc = 0;
EXIT:
	if(pthread_mutex_unlock(&_ctx.al_mutex) < 0)
	    ERROR_RETURN_LOG(int, "Cannot release the async task waiting list mutex");

	if(rc == 0)
	{
		/* Let's notify the async thread on this */
		if(pthread_mutex_lock(&_ctx.q_mutex) < 0)
		    ERROR_RETURN_LOG(int, "Cannot acquire the async processor queue mutex");

		if(pthread_cond_signal(&_ctx.q_cond) < 0)
		    ERROR_RETURN_LOG(int, "Cannot notify the async processing thread on the task ready event");

		if(pthread_mutex_unlock(&_ctx.q_mutex) < 0)
		    ERROR_RETURN_LOG(int, "Cannot release the async processor queue mutex");
	}

	return rc;
}

int sched_async_handle_status_code(runtime_api_async_handle_t* handle, int* resbuf)
{
	_handle_t* task = (_handle_t*)handle;

	if(NULL == task || _HANDLE_MAGIC != task->magic_num || NULL == resbuf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	*resbuf = task->status_code;

	return 0;
}

int sched_async_handle_cancel(runtime_api_async_handle_t* handle, int status)
{
	_handle_t* task = (_handle_t*)handle;

	if(NULL == task || _HANDLE_MAGIC != task->magic_num)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == status)
	    task->status_code = ERROR_CODE(int);

	if(task->state != _STATE_EXEC && task->state != _STATE_INIT)
	    ERROR_RETURN_LOG(int, "Cannot cancel a started async task");

	task->state = _STATE_CANCELED;

	LOG_DEBUG("Async task has been canceled");

	return 0;
}

static int _fake_handle_cntl(_fake_handle_t* handle, uint32_t opcode, va_list ap)
{
	switch(opcode)
	{
		case RUNTIME_API_ASYNC_CNTL_OPCODE_SET_WAIT:
		{
			handle->completed = 0u;
			return 0;
		}
		case RUNTIME_API_ASYNC_CNTL_OPCODE_NOTIFY_WAIT:
		{
			handle->completed = 1u;
			return 0;
		}
		case RUNTIME_API_ASYNC_CNTL_OPCODE_RETCODE:
		{
			int* buf = va_arg(ap, int*);
			*buf = 0;
			return 0;
		}
		case RUNTIME_API_ASYNC_CNTL_OPCODE_CANCEL:
		{
			return 0;
		}
		default:
		    LOG_ERROR("Invalid async_cntl opcode");
	}

	return ERROR_CODE(int);
}

int sched_async_handle_cntl(runtime_api_async_handle_t* handle, uint32_t opcode, va_list ap)
{
	if(NULL == handle || NULL == ap) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(((_fake_handle_t*)handle)->magic_num == _FAKE_HANDLE_MAGIC)
	    return _fake_handle_cntl((_fake_handle_t*)handle, opcode, ap);

	switch(opcode)
	{
		case RUNTIME_API_ASYNC_CNTL_OPCODE_SET_WAIT:
		{
			return sched_async_handle_set_await(handle);
		}
		case RUNTIME_API_ASYNC_CNTL_OPCODE_NOTIFY_WAIT:
		{
			int status_code =  va_arg(ap, int);
			return sched_async_handle_await_complete(handle, status_code);
		}
		case RUNTIME_API_ASYNC_CNTL_OPCODE_RETCODE:
		{
			int* buf = va_arg(ap, int*);
			return sched_async_handle_status_code(handle, buf);
		}
		case RUNTIME_API_ASYNC_CNTL_OPCODE_CANCEL:
		{
			int status_code = va_arg(ap, int);
			return sched_async_handle_cancel(handle, status_code);
		}
		default:
		    LOG_ERROR("Invalid async_cntl opcode");
	}

	return ERROR_CODE(int);
}

runtime_api_async_handle_t* sched_async_fake_handle_new()
{
	_fake_handle_t* ret = (_fake_handle_t*)malloc(sizeof(_fake_handle_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the fake async handle");

	ret->magic_num = _FAKE_HANDLE_MAGIC;
	/* By default it's completed of course, it will set to 0 whenever the wait mode is been select */
	ret->completed = 1u;

	return (runtime_api_async_handle_t*)ret;
}

int sched_async_fake_handle_free(runtime_api_async_handle_t* handle)
{
	if(NULL == handle) ERROR_RETURN_LOG(int, "Invalid arguments");

	_fake_handle_t* h = (_fake_handle_t*)handle;

	if(h->magic_num != _FAKE_HANDLE_MAGIC) ERROR_RETURN_LOG(int, "Invalid fake handle");

	free(h);

	return 0;
}

int sched_async_fake_handle_completed(const runtime_api_async_handle_t* handle)
{
	if(NULL == handle) ERROR_RETURN_LOG(int, "Invalid arguments");

	_fake_handle_t* h = (_fake_handle_t*)handle;

	if(h->magic_num != _FAKE_HANDLE_MAGIC) ERROR_RETURN_LOG(int, "Invalid fake handle");

	return h->completed > 0;
}
