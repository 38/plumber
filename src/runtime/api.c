/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>

#include <utils/log.h>
#include <utils/static_assertion.h>
#include <runtime/api.h>
#include <itc/itc.h>

#include <version.h>
#include <error.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>

#include <sched/async.h>
/**
 * @brief get the current task
 * @param action the action filter checks what kinds of action we expected, if any type of action
 *               is allowed at this point, pass -1
 * @return current task, or NULL indicates error
 **/
static inline runtime_task_t* _get_task(runtime_task_flags_t action)
{
	runtime_task_t* task = runtime_task_current();
	if(NULL == task)
		ERROR_PTR_RETURN_LOG("unable to invoke the servlet API without a running task");

	if(NULL == task->servlet)
		ERROR_PTR_RETURN_LOG("invalid task");

	if(action != (runtime_task_flags_t)-1 && RUNTIME_TASK_FLAG_GET_ACTION(task->flags) != action)
		ERROR_PTR_RETURN_LOG("the operation isn't allowed in this task type (0x%.8x)", task->flags);

	return task;
}

/**
 * @brief get the pipe handle object from the pipe id
 * @param pid the pipe id to get
 * @return the pipe handle
 **/
static inline itc_module_pipe_t* _get_handle(runtime_api_pipe_id_t pid)
{
	runtime_task_t* task = runtime_task_current();
	if(NULL == task) return NULL;

	if(pid == ERROR_CODE(runtime_api_pipe_id_t) || (size_t)pid >= task->npipes)
		ERROR_PTR_RETURN_LOG("invalid Pipe ID %u", pid);

	return task->pipes[pid];
}

static runtime_api_pipe_t _define(const char* name, runtime_api_pipe_flags_t flags, const char* type_expr)
{
	runtime_task_t* task = _get_task(RUNTIME_TASK_FLAG_ACTION_INIT);

	if(NULL == task) return ERROR_CODE(runtime_api_pipe_t);
	runtime_pdt_t* table = task->servlet->pdt;
	int rc = runtime_pdt_insert(table, name, flags, type_expr);
	if(rc == ERROR_CODE(runtime_api_pipe_id_t))
		ERROR_RETURN_LOG(runtime_api_pipe_t, "Cannot insert new entry to PDT");
	return RUNTIME_API_PIPE_FROM_ID((runtime_api_pipe_id_t)rc);
}

/**
 * @brief print the temp fixme message
 * @param type the return type of the function
 **/
#define _FIXME(type) do { LOG_WARNING("FIXME: module service call is not supported"); return ERROR_CODE(type); } while(0)

static size_t _read(runtime_api_pipe_t pipe, void* buffer, size_t nbytes)
{
	if(RUNTIME_API_PIPE_IS_NORMAL(pipe))
	{
		runtime_api_pipe_id_t pid = RUNTIME_API_PIPE_TO_PID(pipe);

		return itc_module_pipe_read(buffer, nbytes, _get_handle(pid));
	}
	else ERROR_RETURN_LOG(size_t, "Service module reference doesn't support read operation");
}


static size_t _write(runtime_api_pipe_t pipe, const void* data, size_t nbytes)
{
	if(RUNTIME_API_PIPE_IS_NORMAL(pipe))
	{
		runtime_api_pipe_id_t pid = RUNTIME_API_PIPE_TO_PID(pipe);

		return itc_module_pipe_write(data, nbytes, _get_handle(pid));
	}
	else ERROR_RETURN_LOG(size_t, "Service module reference doesn't support write operation");
}

static int _write_scope_token(runtime_api_pipe_t pipe, runtime_api_scope_token_t token, const runtime_api_scope_token_data_request_t* data_req)
{
	if(RUNTIME_API_PIPE_IS_NORMAL(pipe))
	{
		runtime_api_pipe_id_t pid = RUNTIME_API_PIPE_TO_PID(pipe);

		return itc_module_pipe_write_scope_token(token, data_req, _get_handle(pid));
	}
	else ERROR_RETURN_LOG(int, "Service module reference doesn't support write operation");
}

static int _eof(runtime_api_pipe_t pipe)
{
	if(RUNTIME_API_PIPE_IS_NORMAL(pipe))
	{
		runtime_api_pipe_id_t pid = RUNTIME_API_PIPE_TO_PID(pipe);

		return itc_module_pipe_eof(_get_handle(pid));
	}
	else ERROR_RETURN_LOG(int, "Service module reference doesn't support eof operation");
}
/* We should assume that the module is 8 bits */
STATIC_ASSERTION_EQ_ID(__itc_module_type_t_size__, sizeof(itc_module_type_t), 1);

static int _cntl(runtime_api_pipe_t pipe, uint32_t opcode, va_list ap)
{
	if(RUNTIME_API_PIPE_IS_NORMAL(pipe))
	{
		runtime_api_pipe_id_t pid = RUNTIME_API_PIPE_TO_PID(pipe);
		return itc_module_pipe_cntl(_get_handle(pid), opcode, ap);
	}
	else
	{
		if(opcode == RUNTIME_API_PIPE_CNTL_OPCODE_INVOKE)
		{
			itc_module_type_t type = (itc_module_type_t)RUNTIME_API_PIPE_VIRTUAL_GET_MODULE(pipe);

			uint32_t module_opcode = RUNTIME_API_PIPE_VIRTUAL_GET_OPCODE(pipe);

			const itc_modtab_instance_t* mi = itc_modtab_get_from_module_type(type);
			if(NULL == mi) ERROR_RETURN_LOG(int, "Invalid module type 0x%x", type);

			if(mi->module->invoke == NULL) ERROR_RETURN_LOG(int, "The module %s doesn't allow invoke operation", mi->path);

			LOG_DEBUG("Invoking the service module function 0x%x at module %s", opcode, mi->path);

			return mi->module->invoke(mi->context, module_opcode, ap);
		}
		else ERROR_RETURN_LOG(int, "Cannot perform operation other than invoke on a service module reference pipe");
	}
}

static const char* _version(void)
{
	return PLUMBER_VERSION;
}

static runtime_api_pipe_t _get_module_func(const char* mod_path, const char* func_name)
{
	if(NULL == mod_path || NULL == func_name) ERROR_RETURN_LOG(runtime_api_pipe_t, "Invalid arguments");

	const itc_modtab_instance_t* mi = itc_modtab_get_from_path(mod_path);

	if(NULL == mi) ERROR_RETURN_LOG(runtime_api_pipe_t, "Cannot found module %s", mod_path);

	runtime_api_pipe_t ret = ((runtime_api_pipe_t)mi->module_id) << 24;
	if(mi->module->get_opcode == NULL) ERROR_RETURN_LOG(runtime_api_pipe_t, "Module callback get_opcode is not supported by module %s", mod_path);
	uint32_t opcode;
	if((opcode = mi->module->get_opcode(mi->context, func_name)) == ERROR_CODE(uint32_t))
		ERROR_RETURN_LOG(runtime_api_pipe_t, "Cannot get the opcode for operation %s", func_name);

	return (ret | opcode);
}

static uint8_t _mod_open(const char* path)
{
	return itc_modtab_get_module_type_from_path(path);
}

static int _mod_cntl_prefix(const char* path, uint8_t* result)
{
	if(NULL == path || NULL == result) ERROR_RETURN_LOG(int, "Invalid arguments");

	itc_modtab_dir_iter_t iter;
	if(itc_modtab_open_dir(path ,&iter) == ERROR_CODE(int))
		ERROR_RETURN_LOG(uint8_t, "Cannot open the module path %s", path);

	/* Because we assume all the prefix should have the same module def, so we use the first
	 * module as the reference to the module instantiated from the same mdoule def */
	const itc_modtab_instance_t* mi = itc_modtab_dir_iter_next(&iter);

	if(NULL == mi)
	{
		/**
		 * In this case, there's no module instances under the directory,
		 * so we just return the ERROR_CODE so that the upper level knows
		 * there's no such module.
		 **/
		*result = ERROR_CODE(uint8_t);
		return 0;
	}

	*result = mi->module_id;

	/* at the same time, we need to make sure all the module in under this directory are using the same module def */
	const itc_modtab_instance_t* cur;
	for(;NULL != (cur = itc_modtab_dir_iter_next(&iter));)
		if(cur->module != mi->module) ERROR_RETURN_LOG(int, "The given directory is not homogeneous");

	return 0;
}

static int _set_type_hook(runtime_api_pipe_t pipe, runtime_api_pipe_type_callback_t callback, void* data)
{
	if(RUNTIME_API_PIPE_IS_NORMAL(pipe))
	{
		runtime_api_pipe_id_t pid = RUNTIME_API_PIPE_TO_PID(pipe);
		runtime_task_t* task = _get_task(RUNTIME_TASK_FLAG_ACTION_INIT);

		if(NULL == task) return ERROR_CODE(int);
		runtime_pdt_t* table = task->servlet->pdt;
		int rc = runtime_pdt_set_type_hook(table, pid, callback, data);

		return rc;
	}
	else ERROR_RETURN_LOG(int, "Cannot set up the type callback for a service module function pipe");
}

static int _async_cntl(runtime_api_async_handle_t* async_handle, uint32_t opcode, va_list ap)
{
	return sched_async_handle_cntl(async_handle, opcode, ap);
}

/**
 * @brief this is the framework address table
 **/
runtime_api_address_table_t runtime_api_address_table = {
	.log_write = log_write_va,
	.define = _define,
	.read   = _read,
	.write = _write,
	.write_scope_token = _write_scope_token,
	.trap = NULL,
	.eof = _eof,
	.cntl = _cntl,
	.version = _version,
	.get_module_func = _get_module_func,
	.mod_open = _mod_open,
	.mod_cntl_prefix = _mod_cntl_prefix,
	.set_type_hook = _set_type_hook,
	.async_cntl = _async_cntl
};


