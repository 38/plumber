/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>

#include <plumber.h>
#include <error.h>
#include <barrier.h>
#include <arch/arch.h>
#include <utils/log.h>
#include <utils/thread.h>

/**
 * @brief the service for the loop
 **/
static const sched_service_t* _service;

/**
 * @brief indicates if all the loop gets killed
 **/
static int _killed = 0;

/**
 * @brief the number of thread
 **/
static uint32_t _nthreads = 1;

/**
 * @brief the size of the event queue for each thread
 **/
static uint32_t _queue_size = SCHED_LOOP_EVENT_QUEUE_SIZE;

/**
 * @brief a scheduler loop context
 **/
typedef struct _context_t {
	struct _context_t* next;         /*!< the next thread in the loop linked list */
	int       started;               /*!< if the loop has started */
	thread_t* thread;                /*!< the thread object */
	uint32_t  thread_id;             /*!< the thread id */
	uint32_t  front;                 /*!< the front pointer of the queue */
	uint32_t  rear;                  /*!< the rear pointer of the queue */
	uint32_t  size;                  /*!< the size of the queue */
	pthread_mutex_t mutex;           /*!< the mutex used with the cond */
	pthread_cond_t  cond;            /*!< the cond var that is used for the loop wait for new event */
	uintptr_t __padding__[0];
	sched_loop_event_t events[0];    /*!< the actual event queue */
} _context_t;
STATIC_ASSERTION_LAST(_context_t, events);
STATIC_ASSERTION_SIZE(_context_t, events, 0);

/**
 * @brief the scheduler list
 **/
static _context_t* _scheds = NULL;

/**
 * @brief indicate if the dispatcher is waiting
 **/
static uint32_t _dispatcher_waiting = 0;

/**
 * @brief the mutex used by the dispatcher
 **/
static pthread_mutex_t _dispatcher_mutex;

/**
 * @brief the condition variable used by the dispatcher
 **/
static pthread_cond_t _dispatcher_cond;

/**
 * @brief the module type for the mem pipe module
 **/
static itc_module_type_t _mod_mem = ERROR_CODE(itc_module_type_t);

static inline _context_t* _context_new(uint32_t tid)
{
	LOG_DEBUG("Creating thread context for scheduler #%d", tid);
	_context_t* ret = (_context_t*)malloc(sizeof(_context_t) + sizeof(sched_loop_event_t) * _queue_size);

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the shceduler thread context");
	ret->started = 0;
	ret->thread_id = tid;
	ret->front = ret->rear = 0;
	ret->size = _queue_size;
	ret->thread = NULL;

	if(pthread_mutex_init(&ret->mutex, NULL) < 0) ERROR_LOG_ERRNO_GOTO(MUTEX_ERR, "Cannot initialize the shceduler local mutex");
	if(pthread_cond_init(&ret->cond, NULL) < 0) ERROR_LOG_ERRNO_GOTO(COND_ERR, "Cannot initialize the scheduler local condvar");

	ret->next = _scheds;
	_scheds = ret;

	return ret;
COND_ERR:
	if(pthread_mutex_destroy(&ret->mutex) < 0)
	    LOG_WARNING_ERRNO("Cannot dispose the pthread mutex");
MUTEX_ERR:
	free(ret);
	return NULL;
}

static inline int _context_free(_context_t* ctx)
{
	int rc = 0;
	if(pthread_mutex_destroy(&ctx->mutex) < 0)
	{
		LOG_ERROR_ERRNO("Cannot dispose the pthread mutex");
		rc = ERROR_CODE(int);
	}

	if(pthread_cond_destroy(&ctx->cond) < 0)
	{
		LOG_ERROR_ERRNO("Cannot dispose the pthread condvar");
		rc = ERROR_CODE(int);
	}

	uint32_t i;
	for(i = ctx->front; i != ctx->rear; i ++)
	{
		uint32_t p = i & (ctx->size - 1);
		if(ctx->events[p].type == SCHED_LOOP_EVENT_TYPE_ITC)
		{
			if(itc_module_pipe_deallocate(ctx->events[p].itc.in) == ERROR_CODE(int))
			{
				LOG_ERROR("Cannot deallocate the input pipe");
				rc = ERROR_CODE(int);
			}
			if(itc_module_pipe_deallocate(ctx->events[p].itc.out) == ERROR_CODE(int))
			{
				LOG_ERROR("Cannot deallocate the output pipe");
				rc = ERROR_CODE(int);
			}
		}
		else LOG_WARNING("Invalid event type");
	}

	free(ctx);

	return rc;
}

static inline void* _sched_main(void* data)
{
	thread_set_name("PbWorker");
	_context_t* context = (_context_t*)data;

	context->started = 1;

	sched_task_context_t* stc = NULL;

	if(NULL == (stc = sched_task_context_new()))
	    ERROR_LOG_ERRNO_GOTO(KILLED, "Cannot initialize the thread locals for the scheduler thread %u", context->thread_id);

	if(ERROR_CODE(int) == sched_rscope_init_thread())
	    ERROR_LOG_ERRNO_GOTO(KILLED, "Cannot initialize the thread locals for request local scope for thread %u", context->thread_id);

	LOG_DEBUG("Scheduler %u: loop started", context->thread_id);


	for(;!_killed;)
	{
		if(context->front == context->rear)
		{
			struct timespec abstime;
			struct timeval now;
			gettimeofday(&now,NULL);
			abstime.tv_sec = now.tv_sec+1;
			abstime.tv_nsec = 0;

			/* TODO: (Async Task) we also want the async task's event wake up the cond var
			 *                    However, since we changed the desgin, we probably do not
			 *                    need to handle this case at this point, because the async
			 *                    task thread should also be a ITC event source.
			 *                    Then we don't need the type of sched_loop_event_t, instead
			 *                    we add another field in itc_equeue_event_t for the task event
			 **/
			if(pthread_mutex_lock(&context->mutex) < 0) LOG_WARNING_ERRNO("Cannot acquire the scheduler event mutex");
			while(context->rear == context->front)
			{
				if(pthread_cond_timedwait(&context->cond, &context->mutex, &abstime) < 0 && errno != ETIMEDOUT && errno != EINTR)
				    LOG_WARNING_ERRNO("Cannot finish pthread_cond_timedwait");
				if(_killed) goto KILLED;
				abstime.tv_sec ++;
			}
			if(pthread_mutex_unlock(&context->mutex) < 0) LOG_WARNING_ERRNO("Cannot release the scheduler event mutex");
		}

		LOG_TRACE("Scheduler Thread %u: new event acquired", context->thread_id);

		/* TODO: Confused - (Async Task) then at this point the thread event queue is not necessarily non-empty any more */

		uint32_t position = context->front & (context->size - 1);
		sched_loop_event_t current = context->events[position];

		BARRIER();

		arch_atomic_sw_increment_u32(&context->front);

		BARRIER();

		if(_dispatcher_waiting)
		{
			if(pthread_mutex_lock(&_dispatcher_mutex) < 0)
			    LOG_WARNING_ERRNO("Cannot lock the dispatcher mutex");

			if(pthread_cond_signal(&_dispatcher_cond) < 0)
			    LOG_WARNING_ERRNO("Cannot notify the dispatcher for the avaliable space");

			if(pthread_mutex_unlock(&_dispatcher_mutex) < 0)
			    LOG_WARNING_ERRNO("Cannot unlock the dispatcher mutex");
		}

		switch(current.type)
		{
			case SCHED_LOOP_EVENT_TYPE_ITC:
			    if(sched_task_new_request(stc, _service, current.itc.in, current.itc.out) == ERROR_CODE(sched_task_request_t))
			        LOG_ERROR("Cannot add the incoming request to scheduler");
			    break;
			default:
			    LOG_WARNING("Invalid schedluer event type");
		}

		while(sched_step_next(stc, _mod_mem) > 0 && !_killed);
	}

KILLED:

	LOG_INFO("Scheduler thread %u gets killed", context->thread_id);

	if(NULL != stc && ERROR_CODE(int) == sched_task_context_free(stc))
	    LOG_WARNING("Cannot finalize the thread locals for scheduler %d", context->thread_id);

	if(ERROR_CODE(int) == sched_rscope_finalize_thread())
	    LOG_WARNING("Cannot finalize the thread locals for the request local scope for thread %u", context->thread_id);

	return NULL;
}

static inline int _start_loop(_context_t* ctx)
{
	if(NULL == (ctx->thread = thread_new(_sched_main, ctx, THREAD_TYPE_WORKER)))
	    ERROR_RETURN_LOG(int, "Cannot start new scheduler thread");

	return 0;
}

static inline int _dispatcher_main()
{

	thread_set_name("PbDispatcher");

	itc_equeue_token_t sched_token = itc_equeue_scheduler_token();

	if(ERROR_CODE(itc_equeue_token_t) == sched_token) ERROR_RETURN_LOG(int, "Cannot acquire the scheduler token");

	_context_t* round_robin_start = _scheds;

	LOG_DEBUG("Dispatcher: loop started");

	for(;!_killed;)
	{
		if(itc_equeue_wait(sched_token, &_killed) == ERROR_CODE(int))
		{
			LOG_WARNING("Cannot wait for the the event queue gets ready");
			continue;
		}

		if(_killed) break;

		//itc_equeue_event_t event;
		sched_loop_event_t event = {
			.type = SCHED_LOOP_EVENT_TYPE_ITC
		};

		if(itc_equeue_take(sched_token, &event.itc) == ERROR_CODE(int))
		{
			LOG_ERROR("Cannot take next event from the event queue");
			continue;
		}

		_context_t* scheduler = round_robin_start;
		int first;

		struct timespec abstime;
		struct timeval now;
		gettimeofday(&now,NULL);
		abstime.tv_sec = now.tv_sec+1;
		abstime.tv_nsec = 0;

		for(;;)
		{
			first = 1;
			for(;(first || scheduler != round_robin_start) &&
			     scheduler->rear - scheduler->front >= scheduler->size;
			     scheduler = scheduler->next == NULL ? _scheds : scheduler->next)
			    first = 0;
			round_robin_start = scheduler->next == NULL ? _scheds : scheduler->next;

			if(scheduler->rear - scheduler->front >= scheduler->size)
			{
				int need_lock = !_dispatcher_waiting;
				arch_atomic_sw_assignment_u32(&_dispatcher_waiting, 1);
				if(need_lock && pthread_mutex_lock(&_dispatcher_mutex) < 0)
				    LOG_WARNING_ERRNO("Cannot acquire the dispatcher mutex");

				if(pthread_cond_timedwait(&_dispatcher_cond, &_dispatcher_mutex, &abstime) < 0 && errno != ETIMEDOUT && errno != EINTR)
				    LOG_WARNING_ERRNO("Cannot complete pthread_cond_timewait");

				abstime.tv_sec ++;

				if(_killed) goto EXIT_LOOP;
			}
			else
			{
EXIT_LOOP:
				if(_dispatcher_waiting)
				{
					arch_atomic_sw_assignment_u32(&_dispatcher_waiting, 0);
					if(pthread_mutex_unlock(&_dispatcher_mutex) < 0)
					    LOG_WARNING_ERRNO("Cannot rlease the dispatcher mutex");
				}

				break;
			}
		}

		if(_killed) break;

		LOG_DEBUG("Round robin dispatcher picked up thread %u", scheduler->thread_id);

		uint32_t p = scheduler->rear & (scheduler->size - 1);
		scheduler->events[p] = event;
		BARRIER();
		int needs_notify = (scheduler->front == scheduler->rear);
		arch_atomic_sw_increment_u32(&scheduler->rear);

		if(needs_notify)
		{
			if(pthread_mutex_lock(&scheduler->mutex) < 0)
			    LOG_WARNING_ERRNO("Cannot acquire the thread local mutex");

			if(pthread_cond_signal(&scheduler->cond) < 0)
			    LOG_WARNING_ERRNO("Cannot notify new incoming event for the scheduler thread %u", scheduler->thread_id);

			if(pthread_mutex_unlock(&scheduler->mutex) < 0)
			    LOG_WARNING_ERRNO("Cannot release the thread local mutex");
		}
	}

	LOG_INFO("Dispatcher gets killed");

	return 0;
}

int sched_loop_start(const sched_service_t* service)
{

	int rc = 0;
	LOG_INFO("Staring the scheduler loop with %d threads", _nthreads);

	if(_mod_mem == ERROR_CODE(itc_module_type_t))
	{
		LOG_DEBUG("Unspecified ITC communication pipe type, use pipe.mem as default");
		_mod_mem = itc_modtab_get_module_type_from_path("pipe.mem");
		if(ERROR_CODE(itc_module_type_t) == _mod_mem)
		    ERROR_RETURN_LOG(int, "Cannot find the module named pipe.mem, aborting");
	}

	if(NULL != _scheds) ERROR_RETURN_LOG_ERRNO(int, "Cannot call the sched loop function twice");

	uint32_t i;
	_context_t* ptr = NULL;

	for(i = 0; i < _nthreads; i ++)
	    if(NULL == _context_new(i))
	        ERROR_LOG_GOTO(CLEANUP_CTX, "Cannot create context for scheduler thread %u", i);

	_service = service;

	if(pthread_mutex_init(&_dispatcher_mutex, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot init the dispatcher mutex");

	if(pthread_cond_init(&_dispatcher_cond, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot init the dispatcher condvar");

	for(ptr = _scheds; ptr != NULL; ptr = ptr->next)
	    if(_start_loop(ptr) == ERROR_CODE(int))
	        LOG_WARNING("Cannot start the scheduler thread %d", ptr->thread_id);
	    else
	        LOG_INFO("Scheduler thread %d is started", ptr->thread_id);

	const sched_service_pipe_descriptor_t* sdesc = sched_service_to_pipe_desc(service);

	itc_module_pipe_param_t request_param = {
		.input_flags = sched_service_get_pipe_flags(service, sdesc->source_node_id, sdesc->source_pipe_desc),
		.output_flags = sched_service_get_pipe_flags(service, sdesc->destination_node_id, sdesc->destination_pipe_desc),
		.input_header = 0,  /* For now all the event pipes are untyped */
		.output_header = 0, /* The same */
		.args = NULL
	};


	if(itc_eloop_start() < 0) ERROR_RETURN_LOG(int, "Cannot start the event loop");

	if(itc_eloop_set_all_accept_param(request_param) == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot set the accept param");


	_dispatcher_main();

CLEANUP_CTX:

	for(ptr = _scheds; ptr != NULL;)
	{
		_context_t* cur = ptr;
		ptr = ptr->next;
		if(cur->started)
		{
			if(thread_free(cur->thread, NULL) < 0)
			{
				LOG_ERROR_ERRNO("Cannot join the thread %d", i);
				rc = ERROR_CODE(int);
			}
		}
		if(_context_free(cur) == ERROR_CODE(int))
		{
			LOG_ERROR("Cannot dispose the context");
			rc = ERROR_CODE(int);
		}
	}
	return rc;
}

int sched_loop_kill()
{
	LOG_INFO("Service gets killed!");

	if(NULL == _scheds) ERROR_RETURN_LOG(int, "Scheduler loops are not started yet");

	_killed = 1;
	return 0;
}

int sched_loop_set_nthreads(uint32_t n)
{
	if(NULL != _scheds)
	    ERROR_RETURN_LOG(int, "Cannot change the number of thread after the loop started");

	_nthreads = n;

	return 0;
}

int sched_loop_set_queue_size(uint32_t size)
{
	_queue_size = 1;
	uint32_t tmp = size;
	for(;tmp > 1; tmp >>= 1, _queue_size <<= 1);
	if(_queue_size < size) _queue_size <<= 1;
	return 0;
}

static inline int _set_prop(const char* symbol, lang_prop_value_t value, const void* data)
{
	(void) data;
	if(NULL == symbol || LANG_PROP_TYPE_ERROR == value.type || LANG_PROP_TYPE_NONE == value.type) 
		ERROR_RETURN_LOG(int, "Invalid arguments");
	if(strcmp(symbol, "nthreads") == 0)
	{
		if(value.type != LANG_PROP_TYPE_INTEGER) ERROR_RETURN_LOG(int, "Type mismatch");
		uint32_t nthreads = (uint32_t)value.num;
		if(sched_loop_set_nthreads(nthreads) < 0) ERROR_RETURN_LOG(int, "Cannot set the number of worker thread");
	}
	else if(strcmp(symbol, "queue_size") == 0)
	{
		if(value.type != LANG_PROP_TYPE_INTEGER) ERROR_RETURN_LOG(int, "Type mismatch");
		uint32_t size = (uint32_t)value.num;
		sched_loop_set_queue_size(size);
	}
	else if(strcmp(symbol, "default_itc_pipe") == 0)
	{
		if(value.type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mistach");
		_mod_mem = itc_modtab_get_module_type_from_path(value.str);
		if(ERROR_CODE(itc_module_type_t) == _mod_mem)
		    ERROR_RETURN_LOG(int, "Cannot find the module named %s, aborting", value.str);
		const itc_modtab_instance_t* inst = itc_modtab_get_from_module_type(_mod_mem);
		if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot get the module instance of module type 0x%x", _mod_mem);

		if(inst->module == NULL) ERROR_RETURN_LOG(int, "Invalid module definition");

		if(inst->module->allocate == NULL) ERROR_RETURN_LOG(int, "Invalid module type: ITC communication module should have accept function");
		LOG_DEBUG("Default ITC communication pipe has been set to %s", value.str);
	}
	else
	{
		LOG_WARNING("Unrecognized symbol name %s", symbol);
		return 0;
	}

	return 1;
}

int sched_loop_init()
{
	lang_prop_callback_t cb = {
		.param = NULL,
		.get   = NULL,
		.set   = _set_prop,
		.symbol_prefix = "scheduler.worker"
	};

	if(ERROR_CODE(int) == lang_prop_register_callback(&cb))
	    ERROR_RETURN_LOG(int, "Cannot register callback for the runtime prop callback");

	return 0;
}

int sched_loop_finalize()
{
	return 0;
}
