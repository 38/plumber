/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <plumber.h>
#include <utils/log.h>
#include <error.h>

static __thread sched_rscope_t* _current_request_scope = NULL;

sched_rscope_t* sched_step_current_scope()
{
	return _current_request_scope;
}

__attribute__((used)) static inline int _run_task_fast(runtime_task_t* task)
{
	if(task->flags & RUNTIME_TASK_FLAG_ACTION_ASYNC)
	    return runtime_task_start_async_cleanup_fast(task);
	return runtime_task_start_exec_fast(task);
}

int sched_step_next(sched_task_context_t* stc, itc_module_type_t type)
{
	sched_task_t* task = NULL;
	uint32_t size, i;
	const sched_service_pipe_descriptor_t* result;
	itc_module_pipe_t *pipes[2];
	int async_post_rc;

	task = sched_task_next_ready_task(stc);

START_OVER:

	if(NULL == task)
	{
		LOG_DEBUG("No task is ready");
		return 0;
	}

	if(NULL == (result = sched_service_get_outgoing_pipes(task->service, task->node, &size)))
	    ERROR_LOG_GOTO(LERR, "Cannot get outgoing pipes");

	/* We should initialize the pipes only for the sync request and the async init */
	int pipe_init = (!runtime_task_is_async(task->exec_task)) || !(task->exec_task->flags & (RUNTIME_TASK_FLAG_ACTION_UNLOAD | RUNTIME_TASK_FLAG_ACTION_EXEC));
	int async_init = pipe_init && runtime_task_is_async(task->exec_task);

	for(i = 0; i < size; i ++)
	{
		if(pipe_init)
		{
			runtime_api_pipe_flags_t out_flags = 0, in_flags = 0;

			out_flags = sched_service_get_pipe_flags(task->service, result[i].source_node_id, result[i].source_pipe_desc);
			if(ERROR_CODE(runtime_api_pipe_flags_t) == out_flags)
			    ERROR_LOG_GOTO(LERR, "Cannot get output pipe flags");

			in_flags = sched_service_get_pipe_flags(task->service, result[i].destination_node_id, result[i].destination_pipe_desc);
			if(ERROR_CODE(runtime_api_pipe_flags_t) == in_flags)
			    ERROR_LOG_GOTO(LERR, "Cannot get input pipe flags");

			size_t input_header_size, output_header_size;

			if(ERROR_CODE(size_t) == (output_header_size = sched_service_get_pipe_type_size(task->service,
			                                                                                result[i].source_node_id,
			                                                                                result[i].source_pipe_desc)))
			    ERROR_LOG_GOTO(LERR, "Cannot get the size of output header size");

			if(ERROR_CODE(size_t) == (input_header_size = sched_service_get_pipe_type_size(task->service,
			                                                                               result[i].destination_node_id,
			                                                                               result[i].destination_pipe_desc)))
			    ERROR_LOG_GOTO(LERR, "Cannot get the size of output header size");

			itc_module_pipe_param_t param = {
				.output_flags  = out_flags,
				.output_header = output_header_size,
				.input_flags   = in_flags,
				.input_header  = input_header_size,
				.args = NULL
			};

			if(out_flags & RUNTIME_API_PIPE_SHADOW)
			{
				runtime_api_pipe_id_t target_pid = RUNTIME_API_PIPE_GET_TARGET(out_flags);
				runtime_api_pipe_flags_t disabled = (out_flags & RUNTIME_API_PIPE_DISABLED);
				pipes[0] = NULL;
				pipes[1] = itc_module_pipe_fork(task->exec_task->pipes[target_pid], in_flags | RUNTIME_API_PIPE_SHADOW | target_pid | disabled, input_header_size, NULL);

				if(ERROR_CODE(int) == sched_task_output_shadow(task, result[i].source_pipe_desc, pipes[1]))
				    ERROR_LOG_GOTO(LERR, "Cannot add the forked pipe as shadow");
			}
			else if(itc_module_pipe_allocate(type, 0, param, pipes + 0, pipes + 1) < 0)
			    ERROR_LOG_GOTO(LERR, "Cannot allocate pipe from <NID = %d, PID = %d> -> <NID = %d, PID = %d>",
			                         result[i].source_node_id, result[i].source_pipe_desc,
			                         result[i].destination_node_id, result[i].destination_pipe_desc);

			if(pipes[0] != NULL && sched_task_output_pipe(task, result[i].source_pipe_desc, pipes[0]) == ERROR_CODE(int))
			    ERROR_LOG_GOTO(LERR, "Cannot assign output pipe to the task");

			if(sched_task_input_pipe(stc, task->service, task->request, result[i].destination_node_id, result[i].destination_pipe_desc, pipes[1], async_init) == ERROR_CODE(int))
			    ERROR_LOG_GOTO(LERR, "Cannot assign the input pipe to the downstream task");
		}
		else if(ERROR_CODE(int) == sched_task_input_pipe(stc, task->service, task->request, result[i].destination_node_id, result[i].destination_pipe_desc, NULL, 1))
		    ERROR_LOG_GOTO(LERR, "Cannot set the async task pipe to ready state");
	}

	if(!async_init)
	{

#ifdef ENABLE_PROFILER
		static __thread int counter = 0;

		if(sched_service_profiler_timer_start(task->service, task->node) == ERROR_CODE(int))
		    LOG_WARNING("Cannot start the profiler");
		counter ++;
#endif
		_current_request_scope = task->scope;
		/* TODO: what should we do for the async task ? */
#ifdef FULL_OPTIMIZATION
		if(_run_task_fast(task->exec_task) == ERROR_CODE(int))
#else
		if(runtime_task_start(task->exec_task) == ERROR_CODE(int))
#endif
		{
#ifdef ENABLE_PROFILER
			if(sched_service_profiler_timer_stop(task->service) == ERROR_CODE(int))
			    LOG_WARNING("Cannot stop the profiler");
#endif
			ERROR_LOG_GOTO(TASK_FAILED, "Task failed");
		}
#ifdef ENABLE_PROFILER
		if(sched_service_profiler_timer_stop(task->service) == ERROR_CODE(int))
		    LOG_WARNING("Cannot stop the profiler");
		if(counter > 10000)
		{
			sched_service_profiler_flush(task->service);
			counter = 0;
		}
#endif
		if(pipe_init == 0)
		{
			/* If the pipe_init flag is 0, the only possible case is we are processing
			 * the async cleanup funciton, in this case, we need to check the async task
			 * status as well */
			int task_rc;
			if(ERROR_CODE(int) == sched_async_handle_status_code(task->exec_task->async_handle, &task_rc))
			    ERROR_LOG_GOTO(TASK_FAILED, "Cannot get the status code of the async task, assume task has failed");

			if(ERROR_CODE(int) == task_rc)
			    ERROR_LOG_GOTO(TASK_FAILED, "The async task status is failed");
		}
		runtime_api_pipe_id_t null_pid = RUNTIME_API_PIPE_TO_PID(task->exec_task->servlet->sig_null);

		if(task->exec_task->pipes[null_pid] != NULL)
		{
			for(i = 0; i < size; i ++)
			{
				int touched = 0;
				if(result[i].source_pipe_desc != task->exec_task->servlet->sig_null &&
				   result[i].source_pipe_desc != task->exec_task->servlet->sig_error &&
				   ERROR_CODE(int) == (touched = itc_module_pipe_is_touched(task->exec_task->pipes[RUNTIME_API_PIPE_TO_PID(result[i].source_pipe_desc)])))
				    ERROR_LOG_GOTO(LERR, "Cannot check if the pipe has been touched");
				if(touched) break;
			}
			if(i == size)
			{
				LOG_DEBUG("The servlet produces zero output, set the __null__ signal");
				size_t rc;
				for(;0 == (rc = itc_module_pipe_write("", 1, task->exec_task->pipes[null_pid])););
				if(ERROR_CODE(size_t) == rc) ERROR_LOG_GOTO(LERR, "Cannot touch the null signal pipe");
			}
		}
	}
	else if(ERROR_CODE(int) == (async_post_rc = sched_task_launch_async(task)))
	{
		/* In this case, we cannot start the async task, so we need notify the downstream right now */
		for(i = 0; i < size; i ++)
			sched_task_input_pipe(stc, task->service, task->request, result[i].destination_node_id, result[i].destination_pipe_desc, NULL, 1);

	    ERROR_LOG_GOTO(TASK_FAILED, "Cannot launch the async task");
	}
	else
	{
		if(async_post_rc == 0)
		{
			/* This indicates the task has been cancelled during the async init is running */
			LOG_DEBUG("Async task has been cancelled, call cleanup directly");
			goto START_OVER;
		}
		else
		{
			LOG_DEBUG("Async task has been started");
			/* In this case, we must not dispose the task, because it's in the pending list */
			goto RETURN;
		}
	}

	goto CLEANUP;
TASK_FAILED:

	/* First, the error code means all the output is not reliable */
	for(i = 0; i < size; i ++)
	{
		if(result[i].source_pipe_desc != task->exec_task->servlet->sig_null &&
		   result[i].source_pipe_desc != task->exec_task->servlet->sig_error &&
		   ERROR_CODE(int) == itc_module_pipe_set_error(task->exec_task->pipes[RUNTIME_API_PIPE_TO_PID(result[i].source_pipe_desc)]))
		    ERROR_LOG_GOTO(LERR, "Cannot set the error state to all the output pipes");
	}

	/* Then we need to touch the error pipe */
	runtime_api_pipe_id_t error_pid = RUNTIME_API_PIPE_TO_PID(task->exec_task->servlet->sig_error);
	if(task->exec_task->pipes[error_pid] != NULL)
	{
		size_t rc;
		for(;0 == (rc = itc_module_pipe_write("", 1, task->exec_task->pipes[error_pid])););
		if(ERROR_CODE(size_t) == rc) ERROR_LOG_GOTO(LERR, "Cannot touch the error signal pipe");
	}

	/* At this point, we are good to go */
CLEANUP:
	if(sched_task_free(task) == ERROR_CODE(int)) LOG_WARNING("Cannot dispose task");

RETURN:

	return 1;
LERR:
	if(task) sched_task_free(task);
	return ERROR_CODE(int);
}
