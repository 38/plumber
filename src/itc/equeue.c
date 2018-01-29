/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <stdlib.h>
#include <plumber.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <error.h>
#include <fallthrough.h>
#include <barrier.h>
#include <arch/arch.h>

#include <utils/vector.h>
#include <utils/static_assertion.h>
#include <utils/log.h>



/**
 * @brief the token used to identify the scheduler thread, which is 0xfffffffe, in order to distinguish with error code
 **/
#define _SCHED_TOKEN (~(itc_equeue_token_t)(1))

/** @brief the mutex used to lock the operation that change the equeue structure */
static pthread_mutex_t _global_mutex;

/** @brief what is the next thread token */
static itc_equeue_token_t _next_token;

/** @brief incidates if the scheduler token has been created, because we only allow call this function once */
static int _sched_token_called;

/**
 * @brief the queue for the single thread
 **/
typedef struct {
	itc_equeue_event_type_t type;        /*!< The event type in this queue (See the notes, although the event mask can
	                                      *   describe multiple types, but we only allow 1 type set here) */
	pthread_mutex_t   mutex;             /*!< the local mutex */
	pthread_cond_t    put_cond;          /*!< the cond variable for equeue_put */
	uint32_t          size;              /*!< the size of the queue */
	uint32_t          front;             /*!< the next avaliable location to write, only module thread with that token can access this */
	uint32_t          rear;              /*!< the next avaliable lication to read, only module thread with schudler token can access this */
	uintpad_t         __padding__[0];
	itc_equeue_event_t events[0];        /*!< the event list */
} _queue_t;

STATIC_ASSERTION_LAST(_queue_t, events);
STATIC_ASSERTION_SIZE(_queue_t, events, 0);

static vector_t* _queues;

/**
 * @brief The mutex used for the disptacher to take an item from the queue
 **/
static pthread_mutex_t _take_mutex;

/**
 * @brief The cond variable used to block the dispatcher when there's no event
 **/
static pthread_cond_t  _take_cond;

/**
 * @biref Indicates if the scheduler is waiting
 **/
volatile uint32_t _sched_waiting;


/**
 * @todo using larger initial size when there's such need for that
 **/
int itc_equeue_init()
{
	int stage = 0;
	if((errno = pthread_mutex_init(&_global_mutex, NULL)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the global mutex");

	stage = 1;
	/* stage 1 */
	_next_token = 0;
	_sched_token_called = 0;
	_queues = vector_new(sizeof(_queue_t*), ITC_EQUEUE_VEC_INIT_SIZE);
	if(NULL == _queues)
	    ERROR_LOG_GOTO(ERR, "Cannot create vector for the queue list");

	stage = 2;
	if((errno = pthread_cond_init(&_take_cond, NULL)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the read condition variable");

	stage = 3;
	if((errno = pthread_mutex_init(&_take_mutex, NULL)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the read condition mutex");

	LOG_DEBUG("Event Queue has been initialized");
	return 0;

ERR:
	switch(stage)
	{
		case 3:
		    pthread_cond_destroy(&_take_cond);
		    FALLTHROUGH();
		case 2:
		    vector_free(_queues);
		    FALLTHROUGH();
		case 1:
		    pthread_mutex_destroy(&_global_mutex);
	}
	return ERROR_CODE(int);
}

int itc_equeue_finalize()
{
	int rc = 0;
	if((errno = pthread_mutex_lock(&_global_mutex)) != 0)
	{
		LOG_ERROR_ERRNO("Cannot lock the global mutex");
		rc = ERROR_CODE(int);
	}
	/* Before we actually stop, we should dispose all the tasks which is still in the queue */
	if(_queues != NULL)
	{
		size_t i;
		for(i = 0; i < vector_length(_queues); i ++)
		{
			_queue_t* queue = *VECTOR_GET(_queue_t*, _queues, i);
			if(NULL != queue)
			{
				uint64_t j;
				for(j = queue->front; j != queue->rear; j ++)
				{
					switch(queue->events[j & (queue->size - 1)].type)
					{
						case ITC_EQUEUE_EVENT_TYPE_IO:
						{
							itc_module_pipe_t* in = queue->events[j & (queue->size - 1)].io.in;
							itc_module_pipe_t* out = queue->events[j & (queue->size - 1)].io.out;

							if(in != NULL && itc_module_pipe_deallocate(in) == ERROR_CODE(int))
							{
								LOG_ERROR("Cannot deallocate the input event pipe");
								rc = ERROR_CODE(int);
							}
							if(out != NULL && itc_module_pipe_deallocate(out) == ERROR_CODE(int))
							{
								LOG_ERROR("Cannot deallocate the output event pipe");
								rc = ERROR_CODE(int);
							}
							break;
						}
						case ITC_EQUEUE_EVENT_TYPE_TASK:
						{
							itc_equeue_task_event_t* event = &queue->events[j & (queue->size - 1)].task;

							/* We don't call the cleanup task at this point for now.
							 * TODO: do we need a way to make it properly cleaned up */

							if(event->async_handle != NULL && ERROR_CODE(int) == sched_async_handle_dispose(event->async_handle))
							{
								LOG_ERROR("Cannot deallocatet the task handle");
								rc = ERROR_CODE(int);
							}

							break;
						}
						default:
						    rc = ERROR_CODE(int);
						    LOG_ERROR("Invalid type of event");
					}
				}
				if((errno = pthread_mutex_destroy(&queue->mutex)) != 0)
				{
					LOG_ERROR_ERRNO("Cannot destroy the queue specified mutex");
					rc = ERROR_CODE(int);
				}
				if((errno = pthread_cond_destroy(&queue->put_cond)) != 0)
				{
					LOG_ERROR_ERRNO("Cannot destroy the queue specified cond variable");
					rc = ERROR_CODE(int);
				}
				free(queue);
			}
		}
		vector_free(_queues);
	}
	if((errno = pthread_mutex_unlock(&_global_mutex)) != 0)
	{
		LOG_ERROR_ERRNO("Cannot destroy the global mutex");
		rc = ERROR_CODE(int);
	}
	if((errno = pthread_mutex_destroy(&_take_mutex)) != 0)
	{
		LOG_ERROR_ERRNO("Cannot destroy the read mutex");
		rc = ERROR_CODE(int);
	}
	if((errno = pthread_cond_destroy(&_take_cond)) != 0)
	{
		LOG_ERROR_ERRNO("Cannot destroy the read cond variable");
		rc = ERROR_CODE(int);
	}
	if((errno = pthread_mutex_destroy(&_global_mutex)) != 0)
	{
		LOG_ERROR_ERRNO("Cannot destroy the global mutex");
		rc = ERROR_CODE(int);
	}
	return rc;
}

itc_equeue_token_t itc_equeue_module_token(uint32_t size, itc_equeue_event_type_t type)
{
	if(size == 0 || type >= ITC_EQUEUE_EVENT_TYPE_COUNT) ERROR_RETURN_LOG(itc_equeue_token_t, "Invalid arguments");

	uint32_t q_size = 1;
	uint32_t tmp = size;
	for(;tmp > 1; tmp >>= 1, q_size <<= 1);
	if(q_size < size) q_size <<= 1;
	LOG_DEBUG("The actual queue size %u", q_size);

	int stage = 0;
	_queue_t* queue;
	vector_t* next;
	itc_equeue_token_t ret;
	if((errno = pthread_mutex_lock(&_global_mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(itc_equeue_token_t, "Cannot lock the global mutex");

	ret = (_next_token ++);
	queue = (_queue_t*)calloc(1, sizeof(_queue_t) + sizeof(itc_equeue_event_t) * q_size);
	if(NULL == queue) ERROR_LOG_GOTO(ERR, "Cannot allocate memory for event queue");

	if((errno = pthread_mutex_init(&queue->mutex, NULL)) != 0) ERROR_LOG_GOTO(ERR, "Cannot initialize the queue mutex");

	stage = 1;

	if((errno = pthread_cond_init(&queue->put_cond, NULL)) != 0) ERROR_LOG_GOTO(ERR, "Cannot initialize the queue cond variable");
	queue->size = q_size;

	stage = 2;
	next = vector_append(_queues, &queue);
	if(NULL == next) ERROR_LOG_GOTO(ERR, "Cannot append the new queue to the queue list");

	_queues = next;
	if((errno = pthread_mutex_unlock(&_global_mutex)) != 0) LOG_WARNING_ERRNO("Cannot release the global mutex");

	LOG_INFO("New module token in the event queue: Token = %x", ret);

	queue->type = type;

	return ret;
ERR:
	switch(stage)
	{
		case 2:
		    pthread_cond_destroy(&queue->put_cond);
		    FALLTHROUGH();
		case 1:
		    pthread_mutex_destroy(&queue->mutex);
	}
	pthread_mutex_unlock(&_global_mutex);
	if(NULL != queue) free(queue);
	return ERROR_CODE(itc_equeue_token_t);
}

itc_equeue_token_t itc_equeue_scheduler_token()
{
	itc_equeue_token_t ret = ERROR_CODE(itc_equeue_token_t);
	if((errno = pthread_mutex_lock(&_global_mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(itc_equeue_token_t, "Cannot acquire the global mutex");

	if(_sched_token_called == 0)
	{
		_sched_token_called = 1;
		ret = _SCHED_TOKEN;
		LOG_INFO("The scheduler token to the caller: Token = %x", ret);
	}
	else LOG_ERROR("Cannot get scheduler token twice");

	if((errno = pthread_mutex_unlock(&_global_mutex)) != 0)
	    LOG_WARNING_ERRNO("cannot release the global mutex");

	return ret;
}

int itc_equeue_put(itc_equeue_token_t token, itc_equeue_event_t event)
{
	if(token == _SCHED_TOKEN)
	    ERROR_RETURN_LOG(int, "Cannot call put method from the scheduler thread");

	if(event.type == ITC_EQUEUE_EVENT_TYPE_IO && (event.io.in == NULL || event.io.out == NULL))
	    ERROR_RETURN_LOG(int, "Invalid IO event");
	else if(event.type == ITC_EQUEUE_EVENT_TYPE_TASK && (event.task.loop == NULL || event.task.task == NULL))
	    ERROR_RETURN_LOG(int, "Invalid Task event");
	else if(event.type != ITC_EQUEUE_EVENT_TYPE_IO && event.type != ITC_EQUEUE_EVENT_TYPE_TASK)
	    ERROR_RETURN_LOG(int, "Invalid event type");

	_queue_t* queue = *VECTOR_GET(_queue_t*, _queues, token);
	if(NULL == queue)
	    ERROR_RETURN_LOG(int, "Cannot get the queue for token %u", token);

	if(queue->type != event.type)
	    ERROR_RETURN_LOG(int, "Invalid event type, the queue do not accept specified event type");

	LOG_DEBUG("token %u: wait for the queue have space for the new event", token);

	struct timespec abstime;
	struct timeval now;
	gettimeofday(&now,NULL);
	abstime.tv_sec = now.tv_sec+1;
	abstime.tv_nsec = 0;

	if(queue->rear == queue->front + queue->size)
	{
		if((errno = pthread_mutex_lock(&queue->mutex)) != 0)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot acquire the queue mutex");

		/* If the queue is currently full, we should make the event loop wait until the scheduler consume at least one event in the queue */
		while(queue->rear == queue->front + queue->size)
		{
			if((errno = pthread_cond_timedwait(&queue->put_cond, &queue->mutex, &abstime)) != 0 && errno != ETIMEDOUT && errno != EINTR)
			    LOG_WARNING_ERRNO("failed to wait for the cond variable get ready");

			if(itc_eloop_thread_killed == 1)
			{
				LOG_INFO("event thread gets killed");
				if((errno = pthread_mutex_unlock(&queue->mutex)) != 0)
				    LOG_WARNING_ERRNO("cannot release the queue mutex");
				return 0;
			}

			abstime.tv_sec ++;
		}

		if((errno = pthread_mutex_unlock(&queue->mutex)) != 0)
		    LOG_WARNING_ERRNO("cannot release the queue mutex");
	}

	LOG_DEBUG("token %u: now the queue have sufficent space for the new event", token);

	uint64_t next_position = (queue->rear) & (queue->size - 1);
	queue->events[next_position] = event;
	/* make sure that the data get ready first, then inc the pointer */
	BARRIER();
	arch_atomic_sw_increment_u32(&queue->rear);

	if(_sched_waiting)
	{
		_sched_waiting = 0;
		LOG_DEBUG("token %u: notifiying the schduler thread to read this element", token);

		/* Signal the take part */
		if((errno = pthread_mutex_lock(&_take_mutex)) != 0)
		    LOG_WARNING_ERRNO("cannot acquire the reader mutex");

		if((errno = pthread_cond_signal(&_take_cond)) != 0)
		    LOG_WARNING_ERRNO("cannot send signal to the scheduler thread");

		if((errno = pthread_mutex_unlock(&_take_mutex)) != 0)
		    LOG_WARNING_ERRNO("cannot release the reader mutex");

		LOG_DEBUG("token %u: event message notified", token);
	}

	return 0;
}

uint32_t itc_equeue_take(itc_equeue_token_t token, itc_equeue_event_mask_t type_mask, itc_equeue_event_t* buffer, uint32_t buffer_size)
{
	uint32_t ret = 0;
	size_t i;
	_queue_t* queue = NULL;

	if(NULL == buffer) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	if(token != _SCHED_TOKEN) ERROR_RETURN_LOG(uint32_t, "Cannot call the take method from event thread");

	/* Find the first queue that is not empty */
	for(i = 0; i < vector_length(_queues); i ++)
	{
		queue = *VECTOR_GET(_queue_t*, _queues, i);

		if(NULL == queue)
		{
			LOG_WARNING("Invalid queue object at %zu, may be a code bug", i);
			continue;
		}

		if(ITC_EQUEUE_EVENT_MASK_ALLOWS(type_mask, queue->type) && queue->front != queue->rear)
		{
			LOG_DEBUG("found queue %zu contains avalible events", i);
			break;
		}
	}

	if(i == vector_length(_queues))
	    ERROR_RETURN_LOG(uint32_t, "Cannot find the event mask = %x", type_mask);
	else LOG_DEBUG("Found events in queue #%zu, take the first one", i);


	for(ret = 0; ret < buffer_size && (queue->rear - queue->front - ret) != 0; ret ++)
	    buffer[ret] = queue->events[(queue->front + ret) & (queue->size - 1)];

	BARRIER();

	queue->front += ret;

	BARRIER();

	if((queue->rear - queue->front + ret) == queue->size)
	{
		LOG_DEBUG("scheduler thread: notifying the more free space in the queue to token %zu", i);
		if((errno = pthread_mutex_lock(&queue->mutex)) != 0)
		    LOG_WARNING_ERRNO("cannot acquire the queue mutex for token %zu", i);

		if((errno = pthread_cond_signal(&queue->put_cond)) != 0)
		    LOG_WARNING_ERRNO("cannot notify the queue cond variable for token %zu", i);

		if((errno = pthread_mutex_unlock(&queue->mutex)) != 0)
		    LOG_WARNING_ERRNO("cannot notify release the queue mutex for token %zu", i);
	}

	return ret;
}

int itc_equeue_empty(itc_equeue_token_t token)
{
	if(token != _SCHED_TOKEN) ERROR_RETURN_LOG(int, "Cannot call this function from the event thread");
	size_t i;
	for(i = 0; i < vector_length(_queues); i ++)
	{
		_queue_t* queue = *VECTOR_GET(_queue_t*, _queues, i);
		if(NULL == queue) ERROR_RETURN_LOG(int, "Cannot get the token local queue %zu", i);
		if(queue->rear != queue->front)
		    return 0;
	}
	return 1;

}

int itc_equeue_wait(itc_equeue_token_t token, const int* killed, itc_equeue_wait_interrupt_t* interrupt)
{
	if(token != _SCHED_TOKEN) ERROR_RETURN_LOG(int, "Cannot call this function from the event thread");

	LOG_DEBUG("The thread is going to be blocked until the queue have at least one event");

	struct timespec abstime;
	struct timeval now;
	gettimeofday(&now,NULL);
	abstime.tv_sec = now.tv_sec+1;
	abstime.tv_nsec = 0;

	int locked = 0;

	itc_equeue_event_mask_t mask = (1u << ITC_EQUEUE_EVENT_TYPE_COUNT) - 1;

	while(killed == NULL || *killed == 0)
	{
		size_t i;

		if(interrupt != NULL && ITC_EQUEUE_EVENT_MASK_NONE == (mask = interrupt->func(interrupt->data)))
		    ERROR_RETURN_LOG(int, "The equeue wait interrupt callback returns an error");

		for(i = 0; i < vector_length(_queues); i ++)
		{
			_queue_t* queue = *VECTOR_GET(_queue_t*, _queues, i);
			if(NULL == queue)
			    LOG_WARNING("Cannot get the queue for token %zu", i);
			else if(ITC_EQUEUE_EVENT_MASK_ALLOWS(mask, queue->type) && queue->rear != queue->front)
			    break;
		}

		if(i != vector_length(_queues)) break;

		if(!locked)
		{

			if((errno = pthread_mutex_lock(&_take_mutex)) != 0)
			    ERROR_RETURN_LOG_ERRNO(int, "Cannot acquire the reader mutex");
			locked = 1;
			_sched_waiting = 1;

			continue;
		}

		if((errno = pthread_cond_timedwait(&_take_cond, &_take_mutex, &abstime)) != 0 && errno != EINTR && errno != ETIMEDOUT)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot wait for the reader condition variable");

		gettimeofday(&now,NULL);
		abstime.tv_sec = now.tv_sec + 1;
	}

	if(locked && (errno = pthread_mutex_unlock(&_take_mutex)) != 0)
	    LOG_WARNING_ERRNO("cannot release the reader mutex");

	if(killed != NULL && *killed)
	{
		LOG_TRACE("Kill message recieved");
		return 0;
	}

	return 0;
}

int itc_equeue_wait_interrupt()
{
	if((errno = pthread_mutex_lock(&_take_mutex)) != 0)
	    LOG_WARNING_ERRNO("cannot acquire the reader mutex");

	if((errno = pthread_cond_signal(&_take_cond)) != 0)
	    LOG_WARNING_ERRNO("cannot send signal to the scheduler thread");

	if((errno = pthread_mutex_unlock(&_take_mutex)) != 0)
	    LOG_WARNING_ERRNO("cannot release the reader mutex");

	return 0;
}
