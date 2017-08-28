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

#include <runtime/api.h>

#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/equeue.h>

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

static uint32_t _handle_magic = 0x35fc32ffu;

typedef enum {
	_STATE_INIT,   /*!< Indicates the task is about to call async_init function */
	_STATE_EXEC,   /*!< The task is about to call async_exec function */
	_STATE_DONE    /*!< The task is about to call asnyc_clean function and emit the event */
} _state_t;

typedef struct {
	uint32_t            magic_num;   /*!< The magic number we used to make sure the task handle valid */
	uint32_t            wait_mode:1; /*!< If this task is in a wait mode */
	sched_async_task_t  task;        /*!< The actual task definition */
} _handle_t;

/**
 * @brief The data structure for the async task queue
 **/
static struct {
	uint32_t             size;   /*!< The size of the queue */
	uint32_t             front;  /*!< The queue front */
	uint32_t             rear;   /*!< The queue rear */
	pthread_mutex_t      mutex;  /*!< The queue mutex */
} _queue;

/**
 * @brief setup the async task processor properties
 * @param symbol The symbol of the property
 * @param value The value to set
 * @param data The data
 * @return status code
 **/
static inline int _set_prop(const char* symbol, lang_prop_value_t value, const void* data)
{
(void)_handle_magic;
(void)_queue;

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
