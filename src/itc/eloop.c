/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include <error.h>
#include <barrier.h>
#include <utils/log.h>
#include <utils/thread.h>
#include <utils/static_assertion.h>
#include <runtime/api.h>
#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/equeue.h>
#include <itc/eloop.h>

/**
 * @brief the thread specified data
 **/
typedef struct {
	itc_module_type_t module_type; /*!< the type code of the module */
	uint8_t buffer_valid:1; /*!< the valid dual buffer */
	itc_module_pipe_param_t accept_param[2]; /*!< the accept param use by this pipe dual buffer */
	uint32_t started:1; /*!< indicates if this thread has been started */
	thread_t* thread; /*!< the thread handle */
	uint32_t killed:1; /*!< indicates if this thread will be killed */
} _thread_data_t;

/**
 * @brief the signal number used to kill the event thread
 * @note it's Ok for others reuse the signal number, because this number only means
 *       make the event thread interrupt current task and check if it's gets killed and
 *       then set the killed  flags. <br/>
 *       So if the signal is external source, we are fine, because no killed bit is set
 */
#define _SIGTHREADKILL SIGUSR1

uint32_t _thread_count;

static __thread _thread_data_t* _self;
__thread uint32_t itc_eloop_thread_killed;

/**
 * @brief the thread data list
 **/
static _thread_data_t* _thread_data;

static inline void* _module_event_loop_main(void* data)
{
	thread_set_name("PbEventLoop");

	_self = (_thread_data_t*)data;


	if(NULL == _self) ERROR_PTR_RETURN_LOG("Invalid arguments");

	itc_eloop_thread_killed = _self->killed;

	LOG_INFO("Starting event loop for module #%u", _self->module_type);

	itc_equeue_token_t token = itc_equeue_module_token(ITC_MODULE_EVENT_QUEUE_SIZE, ITC_EQUEUE_EVENT_TYPE_IO);

	if(ERROR_CODE(itc_equeue_token_t) == token)
	    ERROR_PTR_RETURN_LOG("Cannot allocate token from the event queue from module #%"PRIu32, _self->module_type);

	for(;!_self->killed;)
	{
		itc_equeue_event_t event;

		event.type = ITC_EQUEUE_EVENT_TYPE_IO;

		itc_module_pipe_param_t param = _self->accept_param[_self->buffer_valid];

		if(itc_module_pipe_accept(_self->module_type, param, &event.io.in, &event.io.out) == ERROR_CODE(int))
		{
			itc_module_flags_t flags = itc_module_get_flags(_self->module_type);
			if(ERROR_CODE(itc_module_flags_t) == flags)
			    LOG_ERROR("Cannot get the module flags from module #%u", _self->module_type);
			if(flags & ITC_MODULE_FLAGS_EVENT_EXHUASTED)
			{
				LOG_NOTICE("Event from the module #%u has been exhuasted, exiting the event loop", _self->module_type);
				itc_eloop_thread_killed = _self->killed = 1;
				itc_module_loop_killed(_self->module_type);
				break;
			}
			LOG_ERROR("failed to accept the incoming request from module #%u", _self->module_type);
			continue;
		}

		if(itc_equeue_put(token, event) == ERROR_CODE(int))
		{
			LOG_ERROR("Cannot put the newly received resueat to the event queue");
			continue;
		}
	}

	LOG_NOTICE("Event loop thread has been killed!");

	return data;
}
static inline void _on_thread_killed(int signo)
{
	(void)signo;

	if(_self == NULL) return;

	LOG_INFO("Stopping Event Loop for module %u", _self->module_type);

	if(_self->killed)
	{
		itc_eloop_thread_killed = 1;
		itc_module_loop_killed(_self->module_type);
	}
}

static inline int _init_eloop(void)
{
	if(_thread_data != NULL) ERROR_RETURN_LOG(int, "Event loops are already started");

	itc_module_type_t* modules = NULL;

	if(NULL == (modules = itc_module_get_event_accepting_modules()))
	    ERROR_RETURN_LOG(int, "Cannot get event accepting modules");

	uint32_t i;
	for(i = 0; modules[i] != ERROR_CODE(itc_module_type_t); i ++)
	    LOG_INFO("Found Event Accepting Module: %s", itc_module_get_name(modules[i], NULL, 0));

	_thread_data = (_thread_data_t*)calloc(i, sizeof(_thread_data_t));
	if(NULL == _thread_data)
	{
		free(modules);
		ERROR_RETURN_LOG(int, "Cannot allocate thread data array for the event loops");
	}

	_thread_count = i;
	uint32_t j;
	for(j = 0; j < i; j ++)
	{
		_thread_data[j].module_type = modules[j];
		_thread_data[j].killed = 0;
		_thread_data[j].started = 0;
		_thread_data[j].buffer_valid = 0;
		_thread_data[j].accept_param[0].input_flags = RUNTIME_API_PIPE_INPUT;
		_thread_data[j].accept_param[0].output_flags = RUNTIME_API_PIPE_OUTPUT;
		_thread_data[j].accept_param[0].args = NULL;
	}

	free(modules);

	LOG_DEBUG("ITC Event Loop is successfully intialized");

	return 0;
}

int itc_eloop_start(void)
{
	if(ERROR_CODE(int) == _init_eloop())
	    ERROR_RETURN_LOG(int, "Cannot initialize the event loop");

	struct sigaction act;
	act.sa_handler = _on_thread_killed;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);

	if(sigaction(_SIGTHREADKILL, &act, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot install signal handler");

	uint32_t i;
	for(i = 0; i < _thread_count; i ++)
	{
		if(NULL == (_thread_data[i].thread = thread_new(_module_event_loop_main, _thread_data + i, THREAD_TYPE_EVENT)))
		    LOG_WARNING("Cannot create new thread for the event loop of ITC module %s", itc_module_get_name(_thread_data[i].module_type, NULL, 0));
		else
		{
			_thread_data[i].started = 1;
			LOG_INFO("Event loop for ITC module %s is started", itc_module_get_name(_thread_data[i].module_type, NULL, 0));
		}
	}

	/* block the signal on non-event loop threads */
	sigset_t mask;
	sigemptyset(&mask);

	if(sigaddset(&mask, _SIGTHREADKILL) < 0) LOG_WARNING_ERRNO("Cannot set signal set");

	if((errno = pthread_sigmask(SIG_BLOCK, &mask, NULL)) != 0) LOG_WARNING_ERRNO("Cannot block the _SIGTHREADKILL signal");

	LOG_INFO("Event Loop is successfully started");
	return 0;
}

int itc_eloop_init(void)
{
	return 0;
}
int itc_eloop_finalize(void)
{
	uint32_t i;
	uint32_t has_started = 0;
	for(i = 0;  i < _thread_count; i ++)
	{
		if(_thread_data[i].started) has_started = 1;
		_thread_data[i].killed = 1;
	}
	if(has_started)
	{
		for(i = 0; i < _thread_count; i ++)
		{
			thread_kill(_thread_data[i].thread, _SIGTHREADKILL);
			if(_thread_data[i].started == 0) continue;
			void* ret;
			if(thread_free(_thread_data[i].thread, &ret) < 0)
			{
				LOG_WARNING_ERRNO("cannot join the thread %u", i);
			}
			if(ret == NULL)
			{
				LOG_WARNING("thread %u exited abnomally", i);
			}
			else
			{
				LOG_DEBUG("thread %u exited normally", i);
			}
		}
	}
	free(_thread_data);

	return 0;
}


static inline void _update_accept_param(uint32_t i, itc_module_pipe_param_t param)
{
	/**
	 * The dual buffer isn't a solution at all, think about the following condition:
	 *        EventLoop                  |             Scheduler
	 *                                   |       write to param[1], param[0] is valid
	 * reg <- _thread_data[i].valid(0)   |
	 *                                   |          flip valid, param[1] is valid
	 *                                   |                write to param[0]
	 *      read from param[reg]         |                write to param[0]
	 * But this case is impossible in practice, because no one would change the accept param that frequently
	 **/

	_thread_data[i].accept_param[!_thread_data[i].buffer_valid] = param;
	BARRIER();
	_thread_data[i].buffer_valid = !_thread_data[i].buffer_valid;
}


int itc_eloop_set_accept_param(itc_module_type_t module, itc_module_pipe_param_t param)
{
	uint32_t i;
	for(i = 0; i < _thread_count && _thread_data[i].module_type == module; i ++);

	if(i == _thread_count) ERROR_RETURN_LOG(int, "Module %s is not a event accepting module", itc_module_get_name(module, NULL, 0));

	_update_accept_param(i, param);

	LOG_DEBUG("Accept param for module %s has been successfully updated", itc_module_get_name(module, NULL, 0));
	return 0;
}
int itc_eloop_set_all_accept_param(itc_module_pipe_param_t param)
{
	uint32_t i;
	for(i = 0; i < _thread_count; i ++)
	    _update_accept_param(i, param);

	LOG_DEBUG("Accept param for all module has been successfully updated");
	return 0;
}
