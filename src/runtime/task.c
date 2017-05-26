/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <utils/log.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include <runtime/api.h>
#include <itc/itc.h>

#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>

#include <error.h>

#include <utils/mempool/objpool.h>

/**
 * @brief the variable that use to track the current task
 **/
static __thread runtime_task_t* _current_task = NULL;

/**
 * @brief the mutex used to initialize the memory pool for a servlet
 **/
static pthread_mutex_t _pool_mutex;

int runtime_task_init()
{
	if(pthread_mutex_init(&_pool_mutex, NULL) < 0) ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the pool mutex");
	return 0;
}

int runtime_task_finalize()
{
	if(pthread_mutex_destroy(&_pool_mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot dispose the pool mutex");
	return 0;
}

static inline runtime_task_t* _task_new(runtime_servlet_t* servlet, uint32_t action)
{
	size_t npipes = 0;
	static uint32_t _next_task_id = 0;
	runtime_task_t* ret = NULL;
	if(NULL == servlet) return NULL;
	if((RUNTIME_TASK_FLAG_ACTION_MASK & action) != RUNTIME_TASK_FLAG_ACTION_INIT &&
	   (RUNTIME_TASK_FLAG_ACTION_MASK & action) != RUNTIME_TASK_FLAG_ACTION_UNLOAD)
	{
		npipes = runtime_pdt_get_size(servlet->pdt);
		if(NULL == servlet->task_pool)
		{
			size_t size = sizeof(runtime_task_t) + npipes * sizeof(itc_module_pipe_t*);
			if(pthread_mutex_lock(&_pool_mutex) < 0)
			    LOG_WARNING_ERRNO("Cannot acquire the pool mutex");
			/* Because there may be multiple threads blocked here, so we need to check if the pool is already allocated
			 * after we acquired the lock */
			if(NULL == servlet->task_pool && NULL == (servlet->task_pool = mempool_objpool_new((uint32_t)size)))
			        ERROR_PTR_RETURN_LOG("Cannot create memory pool for the servlet task");
			if(pthread_mutex_unlock(&_pool_mutex) < 0)
			    LOG_WARNING_ERRNO("Cannot release the pool mutex");
		}
		ret = mempool_objpool_alloc(servlet->task_pool);
	}
	else
	    ret = (runtime_task_t*)malloc(sizeof(runtime_task_t));

	if(NULL == ret) return NULL;

	ret->servlet = servlet;
	ret->id = (_next_task_id ++);
	ret->npipes = npipes;
	memset(ret->pipes, 0, npipes * sizeof(itc_module_pipe_t*));
	return ret;
}

int runtime_task_free(runtime_task_t* task)
{
	if(NULL == task) return ERROR_CODE(int);
	unsigned i = 0;
	int rc = 0;

	for(i = 0; i < task->npipes; i ++)
	{
		if(NULL != task->pipes[i] && itc_module_pipe_deallocate(task->pipes[i]) < 0)
		{
			LOG_WARNING("cannot dispose pipe #%d", i);
			rc = ERROR_CODE(int);
		}
	}

	runtime_task_flags_t action = RUNTIME_TASK_FLAG_GET_ACTION(task->flags);
	if(action != RUNTIME_TASK_FLAG_ACTION_INIT && action != RUNTIME_TASK_FLAG_ACTION_UNLOAD)
	    mempool_objpool_dealloc(task->servlet->task_pool, task);
	else
	    free(task);
	return rc;
}

runtime_task_t* runtime_task_new(runtime_servlet_t* servlet, runtime_task_flags_t flags)
{
	if(NULL == servlet || (flags & RUNTIME_TASK_FLAG_ACTION_INVOKED))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	runtime_task_t* ret = _task_new(servlet, RUNTIME_TASK_FLAG_GET_ACTION(flags));

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create task context");

	ret->flags = flags;
	_current_task = ret;

	LOG_TRACE("%s Task (TID = %d) has been created", ret->servlet->bin->name, ret->id);

	return ret;

}

int runtime_task_start_exec_fast(runtime_task_t* task)
{
	LOG_TRACE("%s Task (TID = %d) started", task->servlet->bin->name, task->id);

	_current_task = task;

	if(NULL != task->servlet->bin->define->exec)
	    return task->servlet->bin->define->exec(task->servlet->data);

	return 0;
}

int runtime_task_start(runtime_task_t *task)
{
	if(NULL == task)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(task->flags & RUNTIME_TASK_FLAG_ACTION_INVOKED)
	    ERROR_RETURN_LOG(int, "Trying to launch a executed task");

	LOG_TRACE("%s Task (TID = %d) started", task->servlet->bin->name, task->id);
	task->flags |= RUNTIME_TASK_FLAG_ACTION_INVOKED;
	int rc = ERROR_CODE(int);

	_current_task = task;

	switch(RUNTIME_TASK_FLAG_GET_ACTION(task->flags))
	{
		case RUNTIME_TASK_FLAG_ACTION_INIT:
		    if(NULL != task->servlet->bin->define->init)
		        rc = task->servlet->bin->define->init(task->servlet->argc, (char const* const*)task->servlet->argv, task->servlet->data);
		    else
		        rc = 0;
		    break;
		case RUNTIME_TASK_FLAG_ACTION_EXEC:
		    if(NULL != task->servlet->bin->define->exec)
		        rc = task->servlet->bin->define->exec(task->servlet->data);
		    else
		        rc = 0;
		    break;
		case RUNTIME_TASK_FLAG_ACTION_UNLOAD:
		    if(NULL != task->servlet->bin->define->unload)
		        rc = task->servlet->bin->define->unload(task->servlet->data);
		    else
		        rc = 0;
		    break;
		default:
		    LOG_ERROR("Invalid action bit");
	}

	_current_task = NULL;

	LOG_TRACE("%s Task (TID = %d) exited with status code %d", task->servlet->bin->name, task->id, rc);

	return rc;
}

runtime_task_t* runtime_task_current()
{
	return _current_task;
}

