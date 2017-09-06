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

#include <sched/async.h>

#include <error.h>

#include <utils/mempool/objpool.h>
#include <utils/string.h>

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
	runtime_task_t* ret = NULL;
	if(NULL == servlet) return NULL;
	if((RUNTIME_TASK_FLAG_ACTION_MASK & ~RUNTIME_TASK_FLAG_ACTION_ASYNC & action) != RUNTIME_TASK_FLAG_ACTION_INIT &&
	   (RUNTIME_TASK_FLAG_ACTION_MASK & ~RUNTIME_TASK_FLAG_ACTION_ASYNC & action) != RUNTIME_TASK_FLAG_ACTION_UNLOAD)
	{
		/* Because we use npipes later, so we need to check this outside of the if clause */
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
		
		if(NULL == (ret = mempool_objpool_alloc(servlet->task_pool)))
			ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the new task");

		/* If this is an async servlet and try to creat an exec task, then we need allocate the async data buffer */
		if(servlet->async)
		{
			/* If this is an async servlet, then we need to allocate the async buffer */
			if(servlet->bin->define->async_buf_size > 0)
			{
				if(NULL == servlet->bin->async_pool)
				{
					size_t size = (size_t)servlet->bin->define->async_buf_size;

					if(pthread_mutex_lock(&_pool_mutex) < 0)
						LOG_WARNING_ERRNO("Cannot acquire the pool mutex");

					if(NULL == servlet->bin->async_pool && NULL == (servlet->bin->async_pool = mempool_objpool_new((uint32_t)size)))
						ERROR_LOG_GOTO(ERR, "Cannot create memory pool for the servlet async buffer");

					if(pthread_mutex_unlock(&_pool_mutex) < 0)
						LOG_WARNING_ERRNO("Cannot release the pool mutex");
				}

				if(NULL == (ret->async_data = mempool_objpool_alloc(servlet->bin->async_pool)))
					ERROR_LOG_GOTO(ERR, "Cannot allocate the async data buffer");
			} 
			else ret->async_data = NULL;

			ret->async_owner = 1;
			ret->async_handle = NULL;
		}
		goto EXIT;
ERR:
		if(NULL != ret) mempool_objpool_dealloc(servlet->task_pool, ret);
		ret = NULL;
	}
	else
	    ret = (runtime_task_t*)malloc(sizeof(runtime_task_t));
EXIT:
	if(NULL == ret) return NULL;

	ret->servlet = servlet;
	ret->npipes = npipes;
	memset(ret->pipes, 0, npipes * sizeof(itc_module_pipe_t*));
	return ret;
}

int runtime_task_free(runtime_task_t* task)
{
	if(NULL == task) return ERROR_CODE(int);
	unsigned i = 0;
	int rc = 0;

	/* We actually needs to keep the pipe alive if the task is not the asnyc owner */
	if((task->flags & RUNTIME_TASK_FLAG_ACTION_ASYNC) == 0 || task->async_owner)
	{
		for(i = 0; i < task->npipes; i ++)
		{
			if(NULL != task->pipes[i] && itc_module_pipe_deallocate(task->pipes[i]) < 0)
			{
				LOG_WARNING("cannot dispose pipe #%d", i);
				rc = ERROR_CODE(int);
			}
		}
	}

	/* We only dispose the async data buffer when the owner of this buffer is dead. 
	 * For the convention for who owns the async buffer, please read the documentation
	 * of async_owner field */
	if((task->flags & RUNTIME_TASK_FLAG_ACTION_ASYNC) && task->async_owner)
	{
		if(NULL != task->async_data && ERROR_CODE(int) == mempool_objpool_dealloc(task->servlet->bin->async_pool, task->async_data))
			rc = ERROR_CODE(int);
		if(NULL != task->async_handle && ERROR_CODE(int) == sched_async_handle_dispose(task->async_handle))
			rc = ERROR_CODE(int);
	}


	runtime_task_flags_t action = RUNTIME_TASK_FLAG_GET_ACTION(task->flags);
	if((task->flags & RUNTIME_TASK_FLAG_ACTION_ASYNC) || (action != RUNTIME_TASK_FLAG_ACTION_INIT && action != RUNTIME_TASK_FLAG_ACTION_UNLOAD))
	    mempool_objpool_dealloc(task->servlet->task_pool, task);
	else
	    free(task);
	return rc;
}

/**
 * @brief Get the full name of the task
 * @param task The task
 * @param buf  The result buffer
 * @param size The size of the buffer
 * @return The result string or NULL on error case
 * @note If the buffer is not large enough the name will be truncated autoamtically
 **/
static inline const char* _get_task_full_name(const runtime_task_t* task, char* buf, size_t size)
{
	const char* task_type = NULL;
	if(RUNTIME_TASK_FLAG_ACTION_ASYNC & task->flags)
	{
		switch(RUNTIME_TASK_FLAG_GET_ACTION(task->flags) & ~RUNTIME_TASK_FLAG_ACTION_ASYNC)
		{
			case RUNTIME_TASK_FLAG_ACTION_INIT:
				task_type = "async setup";
				break;
			case RUNTIME_TASK_FLAG_ACTION_EXEC:
				task_type = "async execute";
				break;
			case RUNTIME_TASK_FLAG_ACTION_UNLOAD:
				task_type = "async cleanup";
				break;
		}
	}
	else
	{
		switch(RUNTIME_TASK_FLAG_GET_ACTION(task->flags))
		{
			case RUNTIME_TASK_FLAG_ACTION_INIT:
				task_type = "servlet initialize";
				break;
			case RUNTIME_TASK_FLAG_ACTION_EXEC:
				task_type = "sync execute";
				break;
			case RUNTIME_TASK_FLAG_ACTION_UNLOAD:
				task_type = "servlet unload";
				break;
		}
	}

	if(NULL == task_type) return NULL;

	string_buffer_t sbuf;
	string_buffer_open(buf, size, &sbuf);

	string_buffer_appendf(&sbuf, "%s task [", task_type);

	uint32_t i;
	for(i = 0; i < task->servlet->argc; i ++)
	{
		string_buffer_append(task->servlet->argv[i], &sbuf);
		if(i != task->servlet->argc - 1)
			string_buffer_append(" ", &sbuf);
	}

	string_buffer_append("]", &sbuf);

	return string_buffer_close(&sbuf);
}

runtime_task_t* runtime_task_new(runtime_servlet_t* servlet, runtime_task_flags_t flags)
{
	if(NULL == servlet || (flags & RUNTIME_TASK_FLAG_ACTION_INVOKED))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	runtime_task_t* ret = _task_new(servlet, RUNTIME_TASK_FLAG_GET_ACTION(flags));

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create task context");

	if(RUNTIME_TASK_FLAG_GET_ACTION(flags) == RUNTIME_TASK_FLAG_ACTION_EXEC && servlet->async)
		flags = (flags & ~RUNTIME_TASK_FLAG_ACTION_MASK) | RUNTIME_TASK_FLAG_ACTION_ASYNC | RUNTIME_TASK_FLAG_ACTION_INIT;
	ret->flags = flags;
	_current_task = ret;

#ifdef LOG_TRACE_ENABLED
	char buf[4096];
#endif
	LOG_TRACE("%s has been created", _get_task_full_name(ret, buf, sizeof(buf)));

	return ret;

}

int runtime_task_start_exec_fast(runtime_task_t* task)
{
#ifdef LOG_TRACE_ENABLED
	char buf[4096];
#endif
	LOG_TRACE("%s is started", _get_task_full_name(task, buf, sizeof(buf)));

	_current_task = task;

	if(NULL != task->servlet->bin->define->exec)
		return task->servlet->bin->define->exec(task->servlet->data);

	return 0;
}

int runtime_task_start_async_setup_fast(runtime_task_t* task)
{
#ifdef LOG_TRACE_ENABLED
	char buf[4096];
#endif
	LOG_TRACE("%s is started", _get_task_full_name(task, buf, sizeof(buf)));

	return task->servlet->bin->define->async_setup(task->async_handle, task->async_data, task->servlet->data);
}

int runtime_task_start_async_cleanup_fast(runtime_task_t* task)
{
#ifdef LOG_TRACE_ENABLED
	char buf[4096];
#endif
	LOG_TRACE("%s is started", _get_task_full_name(task, buf, sizeof(buf)));

	return task->servlet->bin->define->async_cleanup(task->async_handle, task->async_data, task->servlet->data);
}

int runtime_task_start(runtime_task_t *task)
{
	if(NULL == task)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(task->flags & RUNTIME_TASK_FLAG_ACTION_INVOKED)
	    ERROR_RETURN_LOG(int, "Trying to launch a executed task");

#ifdef LOG_TRACE_ENABLED
	char buf[4096];
#endif
	LOG_TRACE("%s is started", _get_task_full_name(task, buf, sizeof(buf)));
	task->flags |= RUNTIME_TASK_FLAG_ACTION_INVOKED;
	int rc = ERROR_CODE(int);

	_current_task = task;

	if(0 == (task->flags & RUNTIME_TASK_FLAG_ACTION_ASYNC))
	{
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
	}
	else
	{
		runtime_api_async_handle_t* async_handle = task->async_handle;

		switch(RUNTIME_TASK_FLAG_GET_ACTION(task->flags))
		{
			case RUNTIME_TASK_FLAG_ACTION_INIT:
				if(NULL != task->servlet->bin->define->async_setup)
					rc = task->servlet->bin->define->async_setup(async_handle, task->async_data, task->servlet->data);
				else 
					rc = 0;
				break;
			case RUNTIME_TASK_FLAG_ACTION_EXEC:
				if(NULL != task->servlet->bin->define->async_exec)
					rc = task->servlet->bin->define->async_exec(async_handle, task->async_data);
				else 
					rc = 0;
				break;
			case RUNTIME_TASK_FLAG_ACTION_UNLOAD:
				if(NULL != task->servlet->bin->define->async_cleanup)
					rc = task->servlet->bin->define->async_cleanup(async_handle, task->async_data, task->servlet->data);
				else 
					rc = 0;
				break;
			default:
				LOG_ERROR("Invalid action bit");
		}
	}

	_current_task = NULL;

	LOG_TRACE("%s exited with status code %d", _get_task_full_name(task, buf, sizeof(buf)), rc);

	return rc;
}

runtime_task_t* runtime_task_current()
{
	return _current_task;
}

int runtime_task_async_companions(runtime_task_t* task, runtime_task_t** exec_buf, runtime_task_t** cleanup_buf)
{
	if(NULL == task || NULL == exec_buf || NULL == cleanup_buf || 
	   RUNTIME_TASK_FLAG_GET_ACTION(task->flags) != RUNTIME_TASK_FLAG_ACTION_INIT ||
	   !runtime_task_is_async(task))
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(!task->async_owner) ERROR_RETURN_LOG(int, "Cannot create companion tasks for the async task do not hold the owership of the async buffer");

	if(!task->async_handle) ERROR_RETURN_LOG(int, "Cannot create companions for the async task don't have an associated async handle");

	void* async_buf = task->async_data;
	if(task->servlet->task_pool == NULL) ERROR_RETURN_LOG(int, "Code bug: How can the init task created without task pool ?");

	if(NULL == (*exec_buf = mempool_objpool_alloc(task->servlet->task_pool)))
		ERROR_RETURN_LOG(int, "Cannot allocate the async_exec task object from the task pool");

	if(NULL == (*cleanup_buf = mempool_objpool_alloc(task->servlet->task_pool)))
		ERROR_LOG_GOTO(ERR, "Cannot allocate the async_cleanup task object from the task pool");

	exec_buf[0]->async_data = cleanup_buf[0]->async_data = async_buf;

	exec_buf[0]->servlet = cleanup_buf[0]->servlet = task->servlet;

	exec_buf[0]->async_handle = cleanup_buf[0]->async_handle = task->async_handle;

	exec_buf[0]->flags = cleanup_buf[0]->flags = (task->flags & ~RUNTIME_TASK_FLAG_ACTION_MASK & ~RUNTIME_TASK_FLAG_ACTION_INVOKED);
	exec_buf[0]->flags |= RUNTIME_TASK_FLAG_ACTION_EXEC;
	cleanup_buf[0]->flags |= RUNTIME_TASK_FLAG_ACTION_UNLOAD;
	
	/* We don't allow the exec task access any pipe */
	exec_buf[0]->npipes = 0;
	cleanup_buf[0]->npipes = task->npipes;
	memcpy(cleanup_buf[0]->pipes, task->pipes, sizeof(task->pipes[0]) * task->npipes);

	/* Finally we take the ownership from the async init task and assign it to the cleanup task */
	task->async_owner = 0;
	cleanup_buf[0]->async_owner = 1;
	exec_buf[0]->async_owner = 0;

#ifdef LOG_DEBUG_ENABLED
	char buf[4096];
#endif

	LOG_DEBUG("Created the async exec task companions for task %s", _get_task_full_name(task, buf, sizeof(buf)));

	return 0;
ERR:
	if(NULL != *exec_buf) mempool_objpool_dealloc(task->servlet->task_pool, *exec_buf);
	if(NULL != *cleanup_buf) mempool_objpool_dealloc(task->servlet->task_pool, *cleanup_buf);

	return ERROR_CODE(int);
}
