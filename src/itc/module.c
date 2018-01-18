/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

/**
 * @brief this file actually do not finish a lot of things but basically forwards the calls,
 *        the reason why we have this file is because we want a clear boundary of the ITC_Pipe
 *        system, and this file is the boundary. Beyond that all the code is related to the
 *        property of the pipe itself
 * @file itc/module.c
 **/
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include <utils/log.h>
#include <utils/static_assertion.h>
#include <utils/mempool/objpool.h>
#include <utils/string.h>

#include <runtime/api.h>

#include <itc/itc.h>
#include <itc/module_types.h>

#include <runtime/runtime.h>
#include <sched/sched.h>
#include <lang/lang.h>

#include <module/tcp/module.h>
#include <module/mem/module.h>
#include <module/file/module.h>

#include <error.h>

/**
 * @brief the type of the pipes
 **/
typedef enum {
	_PSTAT_TYPE_INPUT,
	_PSTAT_TYPE_OUTPUT
} _pstat_type_t;
/**
 * @brief the internal state of a pipe
 * @note  We do not track the header state here, because we can reuse the processed_header_size in the pipe handler.
 *        If the processed header size is same as expected header size, this means we are ready to move on, otherwise
 *        we need to consume the header before we read/write the pipe
 **/
typedef struct {
	uint32_t    type:2;       /*!< indicates the type of this pipe */
	uint32_t    accepted:1;   /*!< indicates if this is a pipe created by accept, which means its companion does not share data */
	uint32_t    error:1;      /*!< indicates if this pipe has got an error code */
	uint32_t    i_canclled:1;  /*!< indicates this pipe is cancelled after its creation.
	                               Note that there are two types of cancelled pipes,
	                               this is the case that the task produces this pipe is
	                               not cancelled, howerver, this pipe is totally empty */
	uint32_t    o_touched:1;  /*!< if this input pipe has ever been touched */

	uint32_t    s_hold:1;     /*!< if the shadow pipe is in the hold mode, which means the forked pipe is in a "hold" mode <br/>
	                           *   Because we reuse the forked pipe as both the place holder for the output end of the shadow pipe
	                           *   and the input handle for its input end. <br/>
	                           *   So we actually wants the forked pipe has two different behaviour, <br/>
	                           *   As the place holder, the hold allowed operation should be pipe_cntl, it not allow to write in to it <br/>
	                           *   As the input pipe, it's actually a normal input pipe. <br/>
	                           *   This bit is used to distinguish the two different state: once a pipe has been forked, the newly created
	                           *   pipe will be in hold mode and then after its dispose function has been called, instead of actually dispose
	                           *   it, it will remove this bit.
	                           */
} _pstat_t;
/**
 * @brief define the module handle data structure
 * @todo because the handle may be allocated frequently, so a memory pool is needed
 **/
struct _itc_module_pipe_t {
	void*                      owner;                  /*!< the owner of this pipe */
	itc_module_type_t          module_type;            /*!< the type of the module */
	union {
		_pstat_t               stat;                   /*!< indicates if the pipe is in a ready state */
		uint32_t               stat_flags;             /*!< the numeral flags */
	};
	size_t                     processed_header_size;  /*!< The header data that has been processed */
	size_t                     expected_header_size;   /*!< The expected header size, which can be smaller than the actual header size, since there may be type conversions */
	size_t                     actual_header_size;     /*!< The actual header size */
	struct _itc_module_pipe_t* companion_next;         /*!< a companion is the loop linked list for all the pipes that shares resources */
	struct _itc_module_pipe_t* companion_prev;         /*!< same as companion_next but is the reverse pointer */
	uintpad_t                  __padding_pipe__[0];
	runtime_api_pipe_flags_t   pipe_flags;             /*!< the PDT flags used to create this pipe */
	uintpad_t                  __padding__[0];
	char                       data[0];                /*!< data */
};
STATIC_ASSERTION_TYPE_COMPATIBLE(itc_module_pipe_t, owner, itc_module_pipe_ownership_t, owner);
STATIC_ASSERTION_LAST(itc_module_pipe_t, data);
STATIC_ASSERTION_SIZE(itc_module_pipe_t, data, 0);
/* We assume that the previous padding unit is the pipe flags */
STATIC_ASSERTION_EQ_ID(itc_pipe_flags_position, offsetof(itc_module_pipe_t, pipe_flags), offsetof(itc_module_pipe_t, data) - sizeof(uintpad_t));

/**
 * @brief the data buffer used for the data we need to silently dropped
 **/
static char _junk_buf[4096];

/**
 * @brief get module from the type descriptor
 * @param type the type descriptor of the module
 * @return the result module instnace
 **/
static inline const itc_modtab_instance_t* _get_module_from_type(itc_module_type_t type)
{
	static const itc_modtab_instance_t* _instances[(itc_module_type_t)-1];

	if(type == ERROR_CODE(itc_module_type_t))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(_instances[type] == NULL)
	{
		LOG_DEBUG("The module instance represented by module type 0x%x is unknown, query from module addressing table", type);

		/* We need to use CAS here, because there may be multiple thread at this point, and we only want one thread update this */
		const itc_modtab_instance_t* new_ptr = itc_modtab_get_from_module_type(type);
		if(NULL == new_ptr)
		    ERROR_PTR_RETURN_LOG("Cannot find the module instance with module type descriptor 0x%x", type);

		if(!__sync_bool_compare_and_swap(_instances + type, NULL, new_ptr))
		    LOG_DEBUG("MT: the module instnace table has already been updated");
	}

	return _instances[type];
}

/**
 * @brief get the module instance for the pipe handle
 * @param handle the handle to query
 * @return the module instance
 **/
static inline const itc_modtab_instance_t* _get_module(const itc_module_pipe_t* handle)
{
	if(NULL == handle) return NULL;

	if(ERROR_CODE(itc_module_type_t) == handle->module_type)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	return _get_module_from_type(handle->module_type);
}

/**
 * @brief notify the pipe's companions it has been cancelled
 * @param handle the target handle
 * @return status code
 **/
static inline int _notify_pipe_cancelled(itc_module_pipe_t* handle)
{
	if(NULL == handle) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(handle->stat.type != _PSTAT_TYPE_OUTPUT) ERROR_RETURN_LOG(int, "Could not set cancel state on a input pipe");

	itc_module_pipe_t* affected = handle->companion_next;

	for(;affected != handle; affected = affected->companion_next)
	    if(!affected->stat.i_canclled)
	    {
		    if(affected->owner != NULL && sched_task_input_cancelled(affected->owner) == ERROR_CODE(int))
		        ERROR_RETURN_LOG(int, "Error when trying to notify the ready state");
		    else
		        LOG_DEBUG("Notified its companion because of this cancel state");

		    affected->stat.i_canclled = 1;
	    }
	return 0;
}

/**
 * @brief update the companions' shared pipe flags after the pipe disposed
 * @param handle the handle to dispose
 * @return nothing
 **/
static inline void _update_shared_flags(itc_module_pipe_t* handle)
{
	runtime_api_pipe_flags_t options = (handle->pipe_flags & RUNTIME_API_PIPE_SHARED_MASK);

	itc_module_pipe_t* affected = handle->companion_next;

	for(;affected != handle; affected = affected->companion_next)
	    affected->pipe_flags = (affected->pipe_flags & ~RUNTIME_API_PIPE_SHARED_MASK) | options;
}

itc_module_flags_t itc_module_get_flags(itc_module_type_t type)
{
	const itc_modtab_instance_t* inst = _get_module_from_type(type);

	if(NULL == inst) ERROR_RETURN_LOG(itc_module_flags_t, "Cannot get the module instance from type code 0x%x", type);

	if(inst->module->get_flags != NULL)
	    return inst->module->get_flags(inst->context);

	/* Otherwise the default flag should be returned */
	return 0;
}

itc_module_type_t* itc_module_get_event_accepting_modules()
{
	itc_module_type_t* result = (itc_module_type_t*)calloc((itc_module_type_t)-1, sizeof(itc_module_type_t));
	if(NULL == result)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the event accepting module list");

	size_t num_modules = 0;

	itc_modtab_dir_iter_t iter;

	if(ERROR_CODE(int) == itc_modtab_open_dir("", &iter))
	    ERROR_LOG_GOTO(ERR, "Cannot query the module addressing table");

	const itc_modtab_instance_t* ptr;

	for(;(ptr = itc_modtab_dir_iter_next(&iter)) != NULL;)
	{
		if(ptr->module->get_flags != NULL && (ptr->module->get_flags(ptr->context) & ITC_MODULE_FLAGS_EVENT_LOOP))
		{
			LOG_DEBUG("Module %s is able to accept events", itc_module_get_name(ptr->module_id, NULL, 0));
			result[num_modules ++] = ptr->module_id;
		}
	}

	LOG_TRACE("%zu modules are able to accept event", num_modules);
	result[num_modules] = ERROR_CODE(itc_module_type_t);

	return result;
ERR:
	if(result == NULL) free(result);
	return NULL;
}


int itc_module_init()
{

	itc_modtab_set_handle_header_size(sizeof(itc_module_pipe_t));

	return 0;
}

int itc_module_finalize()
{
	return 0;
}

int itc_module_pipe_is_input(const itc_module_pipe_t* pipe)
{
	if(NULL == pipe) ERROR_RETURN_LOG(int, "Invalid arguments");
	return pipe->stat.type == _PSTAT_TYPE_INPUT;
}

int itc_module_pipe_allocate(itc_module_type_t type, uint32_t hint,
                             const itc_module_pipe_param_t param,
                             itc_module_pipe_t** __restrict out_pipe, itc_module_pipe_t** __restrict in_pipe)
{
	const itc_modtab_instance_t* inst = _get_module_from_type(type);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Invalid module type code 0x%x", type);

	const itc_module_t* module = inst->module;
	void* context = inst->context;
	mempool_objpool_t* pool = inst->handle_pool;

	if(NULL == out_pipe && NULL == in_pipe)
	    ERROR_RETURN_LOG(int, "Invalid arguments: Both input and output buffer are NULL");

	itc_module_pipe_t* __restrict in = (NULL != in_pipe) ? (itc_module_pipe_t*)mempool_objpool_alloc(pool) : NULL;
	itc_module_pipe_t* __restrict out = (NULL != out_pipe) ? (itc_module_pipe_t*)mempool_objpool_alloc(pool) : NULL;

	if((NULL != in_pipe && NULL == in) || (NULL != out_pipe && NULL == out))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for pipe allocation");

	if(NULL != out)
	{
		out->owner = NULL;
		out->module_type = type;
		out->companion_next = out->companion_prev = (NULL == in ? out : in);
		out->stat_flags = 0;
		out->stat.type = _PSTAT_TYPE_OUTPUT;
		/* For the companions, the actual data size must be the size of the data source */
		out->actual_header_size = param.input_header;
		out->expected_header_size = param.output_header;
		out->processed_header_size = 0;
		out->pipe_flags = param.output_flags;
		out->stat.accepted = 0;
		out->stat.error = 0;
	}

	if(NULL != in)
	{
		in->owner = NULL;
		in->module_type = type;
		in->companion_next = in->companion_prev = (NULL == out ? in : out);
		in->stat_flags = 0;
		in->stat.type = _PSTAT_TYPE_INPUT;
		in->actual_header_size = in->expected_header_size = param.input_header;
		in->processed_header_size = 0;
		in->pipe_flags = param.input_flags;
		in->stat.accepted = 0;
		in->stat.error = 0;
	}


	if(module->allocate(context, hint, (NULL == out) ? NULL : out->data, (NULL == in) ? NULL : in->data, param.args) == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "Cannot initialize the pipe instance");

	if(NULL != in_pipe) *in_pipe = in;
	if(NULL != out_pipe) *out_pipe = out;

	return 0;
ERR:
	if(NULL != in) mempool_objpool_dealloc(pool, in);
	if(NULL != out) mempool_objpool_dealloc(pool, out);
	return ERROR_CODE(int);
}

/**
 * @brief get the module for the pipe handle
 * @param module where to put the module
 * @param handle the input handle
 * @param ret the return value
 **/
#define _GET_MODULE(module, handle, ret) \
    const itc_modtab_instance_t* module = _get_module((handle));\
    if(NULL == module) \
    {\
	    LOG_DEBUG("Module call on a unassigned pipe");\
	    return ret;\
    }

/**
 * @brief invoke a module and handle the error code
 * @param type type of the return value
 * @param rc where to put the error code
 * @param module_var the module to invoke
 * @param what what function to invoke
 * @param args the argument list
 **/
#define _INVOKE_MODULE(type, rc, module_var, what, args...) \
    do{\
	    const itc_module_t* __module__ = (module_var->module);\
	    void* __context__ = (module_var->context);\
	    if(__module__->what == NULL)\
	    {\
		    LOG_ERROR("module does not support operation %s", #what);\
		    rc = ERROR_CODE(type);\
	    }\
	    else if((rc = __module__->what(__context__, args)) == ERROR_CODE(type))\
	    {\
		    LOG_ERROR("module function %s exited with an error status", #what);\
		    rc = ERROR_CODE(type);\
	    }\
    }while(0)

/**
 * @brief Skip all the header in this pipe
 * @param handle The pipe handle
 * @param mod The pipe module
 * @return 0 indicates the pipe is not able to read temporarily. 1 indicates the pipe header 
 *         is already stripped. error code for error cases
 **/
static inline int _skip_header(itc_module_pipe_t* handle, const itc_modtab_instance_t* mod)
{
	if(handle->actual_header_size > handle->processed_header_size)
	{
		int rc = 0;
		size_t bytes_to_ignore = handle->actual_header_size - handle->processed_header_size;

		if(mod->module->get_internal_buf != NULL)
		{
			/* At this point we try to use direct buffer access for header skipping */
			size_t header_min_size = bytes_to_ignore;
			size_t header_max_size = bytes_to_ignore;
			const void* skipped = NULL;

			_INVOKE_MODULE(int, rc, mod, get_internal_buf, &skipped, &header_min_size, &header_max_size, handle->data);

			if(ERROR_CODE(int) == rc) 
				ERROR_RETURN_LOG(int, "Cannot get the header buffer");

#ifndef FULL_OPTIMIZATION
			if(rc > 0 && (header_min_size != bytes_to_ignore || header_max_size != bytes_to_ignore))
				ERROR_RETURN_LOG(int, "Module function bug: unexpected memory region size");
#endif
		}

		if(rc == 0)
		{
			LOG_DEBUG("The typed header buffer is not able to consumed by direct buffer access, try to performe normal IO");
			
			while(bytes_to_ignore > 0)
			{
				size_t read_rc;
				size_t bytes_to_read = bytes_to_ignore;
				if(bytes_to_read > sizeof(_junk_buf))
					bytes_to_read = sizeof(_junk_buf);
				_INVOKE_MODULE(size_t, read_rc, mod, read, _junk_buf, bytes_to_read, handle->data);

				if(read_rc == ERROR_CODE(size_t))
				{
					handle->stat.error = 1;
					return ERROR_CODE(int);
				}

				/* If there's no data avaiable for the header, do not polling the pipe */
				if(0 == read_rc) return 0;

				bytes_to_ignore -= read_rc;
				handle->processed_header_size += read_rc;
			}
		}
		else handle->processed_header_size = handle->actual_header_size;
	}
	return 1;
}


int itc_module_pipe_deallocate(itc_module_pipe_t* handle)
{
	_GET_MODULE(mod, handle, 0);

	/* Before we actually do anything yet, we need to make sure if the pipe has been touched and
	 * have incompleted header, fill 0 to the remaining header */
	if(handle->stat.type == _PSTAT_TYPE_OUTPUT &&
	   handle->stat.o_touched &&
	   handle->processed_header_size < handle->actual_header_size)
	{
		size_t bytes_to_fill = handle->actual_header_size - handle->processed_header_size;
		size_t ereased = 0;

		while(bytes_to_fill > 0)
		{
			size_t bytes_to_write = bytes_to_fill;
			if(bytes_to_write > sizeof(_junk_buf))
			    bytes_to_write = sizeof(_junk_buf);

			if(bytes_to_write > ereased)
			{
				memset(_junk_buf + ereased, 0, bytes_to_write - ereased);
				ereased = bytes_to_write;
			}

			size_t rc;

			_INVOKE_MODULE(size_t, rc, mod, write, _junk_buf, bytes_to_write, handle->data);

			if(ERROR_CODE(size_t) == rc)
			    ERROR_RETURN_LOG(int, "Cannot fill zeros to the buffer");

			bytes_to_fill -= rc;
			handle->processed_header_size += rc;
		}
	}


	/* Check if its an hold shadow, then decide if we need to cancel the downstreams */
	if(handle->stat.s_hold)
	{
		LOG_DEBUG("Disposing the output-end of a shadow pipe, just remove the hold flag");
		handle->stat.s_hold = 0;
		if((handle->pipe_flags & RUNTIME_API_PIPE_DISABLED) && !handle->stat.i_canclled)
		{
			handle->stat.i_canclled = 1;
			if(ERROR_CODE(int) == sched_task_input_cancelled(handle->owner))
			    ERROR_RETURN_LOG(int, "Cannot cancel the disabled shadow task");
			else
			    LOG_DEBUG("The pipe has been successully cancelled because of the disabled falg");
		}
		return 0;
	}


	int rc = 0;
	int last = (handle->companion_next == handle); /* If the loop size is 1, we should pruge the entire data */
	mempool_objpool_t* pool = mod->handle_pool;

	/* Before its death, we should notify all its companions that it's ready to consume */
	if(!last)   /* if this is not the last one in the loop, we should notify others */
	{
		if(!handle->stat.accepted)   /* if this is an allocated pipe */
		{
			if(handle->stat.type == _PSTAT_TYPE_OUTPUT)   /* and it's the upstream pipe */
			{
				_update_shared_flags(handle);
				if((!handle->stat.o_touched || handle->stat.error) && _notify_pipe_cancelled(handle) == ERROR_CODE(int))
				    LOG_WARNING("Cannot cancel the unused pipe");
			}
		}
		else if(handle->stat.type == _PSTAT_TYPE_INPUT)   /* if it's an accepted pipe and this is the upstream side of the pipe */
		    _update_shared_flags(handle); /* pass the shared flags to it's companions */
	}

	_INVOKE_MODULE(int, rc, mod, deallocate, handle->data, handle->stat.error?1:0, last);

	handle->companion_prev->companion_next = handle->companion_next;
	handle->companion_next->companion_prev = handle->companion_prev;
	handle->companion_next->stat.error = (handle->stat.error || handle->companion_next->stat.error);

	if(mempool_objpool_dealloc(pool, handle) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot deallocate the used pipe handle");

	return rc;
}

size_t itc_module_pipe_read(void* buffer, size_t nbytes, itc_module_pipe_t* handle)
{
	_GET_MODULE(mod, handle, 0);

	if(handle->pipe_flags & RUNTIME_API_PIPE_DISABLED) return 0;

	if(handle->stat.s_hold) ERROR_RETURN_LOG(size_t, "Cannot read from a shadow output pipe");

	if(handle->stat.type != _PSTAT_TYPE_INPUT) ERROR_RETURN_LOG(size_t, "Wrong pipe type");

	size_t rc = 0;
	int src = 0;

	if((src = _skip_header(handle, mod)) == ERROR_CODE(int))
		ERROR_RETURN_LOG(size_t, "Cannot skip the header");

	if(src == 0) 
	{
		LOG_DEBUG("Header data is not avaiable, the pipe is waiting for the header data");
		return 0;
	}

	rc = 0;

	/* If we have processed all the typed header, so we can safely go ahead */
	_INVOKE_MODULE(size_t, rc, mod, read, buffer, nbytes, handle->data);

	if(rc == ERROR_CODE(size_t)) handle->stat.error = 1;
	return rc;
}

/**
 * @brief the actual implementation for the write api call
 * @param data the data to write
 * @param nbytes how many bytes to write
 * @param handle the pipe to write
 * @param mod the module instance we should use
 * @return the number of bytes has written
 **/
static inline size_t _write_impl(const void* data, size_t nbytes, itc_module_pipe_t* handle, const itc_modtab_instance_t * mod)
{
	if(handle->stat.type != _PSTAT_TYPE_OUTPUT) ERROR_RETURN_LOG(size_t, "Wrong pipe type");

	size_t rc = 0;
	size_t bytes_to_fill = handle->actual_header_size - handle->processed_header_size;
	size_t filled_start = 0;

	if(bytes_to_fill > 0)
	    LOG_DEBUG("There are %zu bytes unwritten pipe header, fill zero before move on", bytes_to_fill);

	while(bytes_to_fill > 0)
	{
		size_t bytes_to_write = bytes_to_fill;
		if(bytes_to_write > sizeof(_junk_buf))
		    bytes_to_write = sizeof(_junk_buf);

		if(filled_start < bytes_to_write)
		{
			memset(_junk_buf + filled_start, 0, bytes_to_write - filled_start);
			filled_start = bytes_to_write;
		}

		_INVOKE_MODULE(size_t, rc, mod, write, _junk_buf, bytes_to_write, handle->data);
		if(rc == ERROR_CODE(size_t))
		{
			handle->stat.error = 1;
			return rc;
		}

		if(rc == 0) break;

		handle->processed_header_size += rc;
		bytes_to_fill -= rc;
	}

	/* This means we don't have anything to write */
	if(bytes_to_fill > 0)
	{
		LOG_DEBUG("The header IO are still undergoing, we are not able to write any body data");
		return 0;
	}

	rc = 0;

	_INVOKE_MODULE(size_t, rc, mod, write, data, nbytes, handle->data);

	if(rc == ERROR_CODE(size_t)) handle->stat.error = 1;

	/* This pipe has been touched, which means it's not an empty pipe */
	if(rc != ERROR_CODE(size_t) && rc > 0) handle->stat.o_touched = 1;
	return rc;
}

size_t itc_module_pipe_write(const void* data, size_t nbytes, itc_module_pipe_t* handle)
{
	/* Since we discard everything, so we want to tell user-space program, ok,
	 * I have taken care of everything already */
	_GET_MODULE(mod, handle, nbytes);

	return _write_impl(data, nbytes, handle, mod);
}

/**
 * @brief the read function wrapper for a request local scope stream interface
 * @param handle the RLS stream
 * @param buffer the buffer to return the result
 * @param count how many bytes we want to read
 * @return the number of bytes we actually read
 **/
static inline size_t _rls_stream_read(void* __restrict handle, void* __restrict buffer, size_t count)
{
	return sched_rscope_stream_read((sched_rscope_stream_t*)handle, buffer, count);
}

/**
 * @brief check if the RLS stream reached the end
 * @param handle the RLS stream
 * @return result or error code
 **/
static inline int _rls_stream_eos(const void* handle)
{
	return sched_rscope_stream_eos((const sched_rscope_stream_t*)handle);
}

/**
 * @brief close a used RLS stream
 * @param handle the RLS stream
 * @return status code
 **/
static inline int _rls_stream_close(void* handle)
{
	return sched_rscope_stream_close((sched_rscope_stream_t*)handle);
}

int itc_module_pipe_write_scope_token(runtime_api_scope_token_t token, const runtime_api_scope_token_data_request_t* data_req, itc_module_pipe_t* handle)
{
	sched_rscope_stream_t* stream = NULL;

	stream = sched_rscope_stream_open(token);
	if(NULL == stream)
	    ERROR_RETURN_LOG(int, "Cannot open the token stream for read");

	itc_module_data_source_t data_source = {
		.data_handle = stream,
		.read = _rls_stream_read,
		.eos  = _rls_stream_eos,
		.close = _rls_stream_close
	};


	int rc = itc_module_pipe_write_data_source(data_source, data_req, handle);

	/* Do not dispose the stream when the error code is with ownership transferring */
	if((rc == 0 || rc == ERROR_CODE(int)) && ERROR_CODE(int) == sched_rscope_stream_close(stream))
	    ERROR_RETURN_LOG(int, "Cannot dispose the used callback stream");
	/* And we need to convert this to normal error code */
	if(rc == ERROR_CODE_OT(int))
	    return ERROR_CODE(int);

	return 0;
}

int itc_module_pipe_write_data_source(itc_module_data_source_t data_source, const runtime_api_scope_token_data_request_t* data_req, itc_module_pipe_t* handle)
{
	if(NULL == data_source.read || NULL == data_source.close || NULL == data_source.eos)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(handle->stat.type != _PSTAT_TYPE_OUTPUT)
	    ERROR_RETURN_LOG(int, "Wrong pipe type");

	/* If it's an unassigned pipe, so we tell the caller,
	 * well we are ok with the callback, and we are done with it
	 * please go ahead and dispose it */
	_GET_MODULE(mod, handle, 0);

	char buf[ITC_MODULE_CALLBACK_READ_BUF_SIZE];
	if(NULL != data_req)
	{
		LOG_DEBUG("Token stream data request is defined, will consume at most %zu bytes with the callback", data_req->size);
		size_t size_rem;
		for(size_rem = data_req->size; size_rem > 0;)
		{
			size_t bytes_to_read = size_rem;
			if(bytes_to_read > sizeof(buf))
			    bytes_to_read = sizeof(buf);

			int eos_rc = data_source.eos(data_source.data_handle);
			if(ERROR_CODE(int) == eos_rc)
			    ERROR_RETURN_LOG(int, "end-of-stream call returns an error code");
			else if(1 == eos_rc)
			{
				LOG_DEBUG("RLS token stream is exhuasted by the data request, no DRA needed");
				return 0;
			}

			size_t bytes_read = data_source.read(data_source.data_handle, buf, bytes_to_read);
			if(ERROR_CODE(size_t) == bytes_read || bytes_read > bytes_to_read)
			    ERROR_RETURN_LOG(int, "Cannot read the token stream");

			size_rem -= bytes_read;

			char* begin;
			for(begin = buf; bytes_read;)
			{
				size_t processed = data_req->data_handler(data_req->context, begin, bytes_read);
				if(ERROR_CODE(size_t) == processed)
				    LOG_WARNING("Data request handler failure");
				else if(bytes_read < processed)
				    LOG_WARNING("Inavlid data handle return size %zu, limit is %zu", processed, bytes_read);

				if(0 == processed)
				    LOG_DEBUG("Data request handler rejected the new feed, stopping the request");

				if(0 == processed || ERROR_CODE(size_t) == processed || bytes_read < processed)
				{
					for(;bytes_read;)
					{
						size_t write_rc = _write_impl(begin, bytes_read, handle, mod);
						if(ERROR_CODE(size_t) == write_rc)
						    ERROR_RETURN_LOG(int, "Cannot write data to pipe");
						bytes_read -= write_rc;
						begin += write_rc;
					}
					goto DR_END;
				}
				begin += processed;
				bytes_read -= processed;
			}
		}
	}
DR_END:

	if(mod->module->write_callback == NULL)
	{
		LOG_DEBUG("The module %s doesn't support write_callback module call, using normal write simulate the API",
		           mod->path);
		for(;;)
		{
			int eos_rc = data_source.eos(data_source.data_handle);
			if(ERROR_CODE(int) == eos_rc)
			    ERROR_RETURN_LOG(int, "end-of-stream call returns an error");
			else if(1 == eos_rc) break;

			char* begin;
			size_t bytes_read = data_source.read(data_source.data_handle, begin = buf, sizeof(buf));
			if(ERROR_CODE(size_t) == bytes_read)
			    ERROR_RETURN_LOG(int, "Cannot read the token stream");

			for(;bytes_read;)
			{
				size_t write_rc = _write_impl(begin, bytes_read, handle, mod);
				if(ERROR_CODE(size_t) == write_rc)
				    ERROR_RETURN_LOG(int, "Cannot write data to pipe");
				bytes_read -= write_rc;
				begin += write_rc;
			}
		}

		return 0;
	}
	else
	{

		int rc = 0;

		/* The function itself do not close the stream, but the module may do so */

		_INVOKE_MODULE(int, rc, mod, write_callback, data_source, handle->data);

		/* TODO: we may want do distinguish the resource failure and the callback failure,
		 *       for callback failure, there's no reason for us to close the connection
		 **/
		if(rc == ERROR_CODE(int) || rc == ERROR_CODE_OT(int))
		{
			LOG_DEBUG("The write_callback module call returns an error, disposing the stream");
			handle->stat.error = 1;
			return rc;
		}
		else
		{
			handle->stat.o_touched = 1;
			return 1;
		}
	}
}

int itc_module_pipe_accept(itc_module_type_t type, itc_module_pipe_param_t param,
                           itc_module_pipe_t** __restrict in_pipe, itc_module_pipe_t** __restrict out_pipe)
{
	const itc_modtab_instance_t* inst = _get_module_from_type(type);

	if(NULL == inst) ERROR_RETURN_LOG(int, "Invalid type code 0x%x", type);

	mempool_objpool_t* pool = inst->handle_pool;
	const itc_module_t* module = inst->module;
	void* context = inst->context;

	if(module->accept == NULL) ERROR_RETURN_LOG(int, "Module doesn't support accept");

	itc_module_pipe_t* __restrict in = (itc_module_pipe_t*)mempool_objpool_alloc(pool);
	itc_module_pipe_t* __restrict out = (itc_module_pipe_t*)mempool_objpool_alloc(pool);

	if(NULL == in || NULL == out) ERROR_LOG_GOTO(ERR, "Cannot allcoate memory for the new pipe");

	out->module_type = in->module_type = type;

	in->stat_flags = out->stat_flags = 0;

	in->companion_next = in->companion_prev = out;
	in->pipe_flags = param.input_flags;
	in->stat.type = _PSTAT_TYPE_INPUT;
	/* For the source-sink pair, because there's no actual data dependency, so the actual header size
	 * and the expected header size are the same */
	in->actual_header_size = in->expected_header_size = param.input_header;
	in->processed_header_size = 0;

	out->companion_next = out->companion_prev = in;
	out->pipe_flags = param.output_flags;
	out->stat.type = _PSTAT_TYPE_OUTPUT;
	/* Same as the argument for the input end */
	out->actual_header_size = out->expected_header_size = param.output_header;
	out->processed_header_size = 0;

	in->stat.accepted = out->stat.accepted = 1;
	in->stat.error = out->stat.error = 0;

	/* Initialize the flag first, so we can pass the flags to the slave module in the accept callback */
	if(module->accept(context, param.args, in->data, out->data) < 0)
	{
		if(!itc_eloop_thread_killed)
		    LOG_ERROR("Could not listen from the module %s", itc_module_get_name(type, NULL, 0));
		goto ERR;
	}

	*in_pipe = in;
	*out_pipe = out;

	return 1;
ERR:
	if(NULL != in) mempool_objpool_dealloc(pool, in);
	if(NULL != out) mempool_objpool_dealloc(pool, out);
	return ERROR_CODE(int);
}

static inline int _has_output_companion(const itc_module_pipe_t* handle)
{
	if(handle->stat.type == _PSTAT_TYPE_OUTPUT) return 1;
	const itc_module_pipe_t* ptr;
	for(ptr = handle->companion_next; handle != ptr && ptr->stat.type != _PSTAT_TYPE_OUTPUT; ptr = ptr->companion_next);

	return ptr->stat.type == _PSTAT_TYPE_OUTPUT;
}

int itc_module_pipe_eof(itc_module_pipe_t* handle)
{
	if(NULL == handle) return 1;

	if(handle->stat.type != _PSTAT_TYPE_INPUT)
	    ERROR_RETURN_LOG(int, "Invalid arguments: wrong pipe direction");

	/* For a disabled downstream, even if the task gets a chance to run, we still need to pretent there's no data at all */
	if(handle->pipe_flags & RUNTIME_API_PIPE_DISABLED) return 1;

	/* if this pipe do not have a outputing end (producer), that means if
	 * the handle current do not contain any unread data, it should
	 * be a EOF case.
	 * If this handle is created by accept(), which means it doesn't
	 * share data between it companion, so we call module.has_more function
	 **/
	if(handle->stat.accepted || !_has_output_companion(handle))
	{
		/* Of course, for an unassigned pipe, this is the end of the world ! */
		_GET_MODULE(mod, handle, 1);

		int rc = 0;
		_INVOKE_MODULE(int, rc, mod, has_unread_data, handle->data);

		if(rc == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Has_unread_data return an error code");

		return !rc;
	}

	return 0;
}

static inline int _get_header_buf(void const** result, size_t nbytes, itc_module_pipe_t* handle)
{
	if(NULL == result)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_GET_MODULE(mod, handle, 0);

	if(handle->stat.s_hold) ERROR_RETURN_LOG(int, "Cannot read from a shadow output pipe");

	if(handle->stat.type != _PSTAT_TYPE_INPUT) ERROR_RETURN_LOG(int, "Wrong pipe type");

	if(mod->module->get_internal_buf == NULL)
	{
		LOG_DEBUG("The module doesn't support direct buffer access");
		return 0;
	}

	if(handle->expected_header_size > nbytes + handle->processed_header_size)
	{
		LOG_DEBUG("The pipe header doesn't contains enough data");
		return 0;
	}

	int rc = 0;
	size_t max_size, min_size;
	max_size = min_size = nbytes;

	_INVOKE_MODULE(int, rc, mod, get_internal_buf, result, &min_size, &max_size, handle->data);

	if(rc == ERROR_CODE(int))
	{
		handle->stat.error = 1;
		return ERROR_CODE(int);
	}
	else if(rc == 0)
	{
		LOG_DEBUG("Getting header buffer in such size is not possible, returning empty");
		return 0;
	}

	if(min_size != max_size || min_size != nbytes)
	{
		LOG_ERROR("Code bug: unexpected module behavior: (module:%s, function: get_internal_buf) returns unexpected range", 
				   mod->path);
		/* TODO: In this case if we need to go ahead ? */
		handle->stat.error = 1;
		return ERROR_CODE(int);
	}

	handle->processed_header_size += nbytes;

	return 1;
}


static inline int _get_data_body_buf(void const** result, size_t* min_size, size_t* max_size, itc_module_pipe_t* handle)
{
	if(NULL == result || NULL == min_size || NULL == max_size) 
		ERROR_RETURN_LOG(int, "Invalid arguments");
	
	_GET_MODULE(mod, handle, 0);

	if(handle->stat.s_hold) ERROR_RETURN_LOG(int, "Cannot read from a shadow output pipe");

	if(handle->stat.type != _PSTAT_TYPE_INPUT) ERROR_RETURN_LOG(int, "Wrong pipe type");

	if(mod->module->get_internal_buf == NULL)
	{
		LOG_DEBUG("The module doesn't support direct buffer access");
		return 0;
	}

	int rc = _skip_header(handle, mod);

	if(rc == 0) return 0;
	else if(rc == ERROR_CODE(int))
		ERROR_RETURN_LOG(int, "Cannot skip the header");

	rc = 0;
	
	_INVOKE_MODULE(int, rc, mod, get_internal_buf, result, min_size, max_size, handle->data);
	
	if(rc == ERROR_CODE(int))
	{
		handle->stat.error = 1;
		return ERROR_CODE(int);
	}
	else if(rc == 0)
	{
		LOG_DEBUG("Getting header buffer in such size is not possible, returning empty");
		return 0;
	}

	return 1;
}

static inline size_t _read_header(void* buffer, size_t nbytes, itc_module_pipe_t* handle)
{
	if(NULL == buffer || nbytes == ERROR_CODE(size_t))
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	_GET_MODULE(mod, handle, 0);

	if(handle->stat.s_hold) ERROR_RETURN_LOG(size_t, "Cannot read from a shadow output pipe");

	if(handle->stat.type != _PSTAT_TYPE_INPUT) ERROR_RETURN_LOG(size_t, "Wrong pipe type");

	if(handle->processed_header_size >= handle->expected_header_size)
	{
		LOG_DEBUG("The pipe header is exhuasted, exiting");
		return 0;
	}

	size_t bytes_to_read = handle->expected_header_size - handle->processed_header_size;
	if(bytes_to_read > nbytes)
	    bytes_to_read = nbytes;

	size_t rc = 0;

	/* If we have processed all the typed header, so we can safely go ahead */
	_INVOKE_MODULE(size_t, rc, mod, read, buffer, bytes_to_read, handle->data);

	if(rc == ERROR_CODE(size_t))
	    handle->stat.error = 1;
	else
	    handle->processed_header_size += rc;

	return rc;
}

static inline size_t _write_header(const void* data, size_t nbytes, itc_module_pipe_t* handle)
{
	if(NULL == data || ERROR_CODE(size_t) == nbytes)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	_GET_MODULE(mod, handle, 0);

	if(handle->stat.type != _PSTAT_TYPE_OUTPUT) ERROR_RETURN_LOG(size_t, "Wrong pipe type");

	/* For the output case, the execpted header and actual header are always the same */
	if(nbytes + handle->processed_header_size > handle->expected_header_size)
	{
		LOG_DEBUG("Typed header truncated, %zu bytes has been dropped", nbytes + handle->processed_header_size - handle->expected_header_size);
		nbytes = handle->expected_header_size - handle->processed_header_size;
	}

	size_t rc = 0;

	_INVOKE_MODULE(size_t, rc, mod, write, data, nbytes, handle->data);

	if(rc == ERROR_CODE(size_t))
	    handle->stat.error = 1;
	else
	    handle->processed_header_size += rc;

	/* This pipe has been touched, which means it's not an empty pipe */
	if(rc != ERROR_CODE(size_t) && rc > 0) handle->stat.o_touched = 1;
	return rc;
}

int itc_module_pipe_cntl(itc_module_pipe_t* handle, uint32_t opcode, va_list ap)
{
	if(NULL == handle) return 0;

	itc_module_type_t opcode_type = (itc_module_type_t)RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(opcode);
#define _INVOKE_NULLABLE(rc_type, rc_error, what, args...) \
	_GET_MODULE(mod, handle, 0);\
	rc_type rc = (rc_type)0;\
	if(mod->module->what == NULL) return 0;\
	_INVOKE_MODULE(rc_type, rc, mod, what, handle->data, ##args);\
	if(rc == rc_error) ERROR_RETURN_LOG(int, "Cannot finish module call"#what);


	if(opcode_type == ERROR_CODE(itc_module_type_t))
	{
		switch(opcode)
		{
			case RUNTIME_API_PIPE_CNTL_OPCODE_GET_FLAGS:
			{
				/* For this case, we have parameter to store result */
				runtime_api_pipe_flags_t* result = va_arg(ap, runtime_api_pipe_flags_t*);
				if(NULL == result) ERROR_RETURN_LOG(int, "Invalid arguments");
				*result = handle->pipe_flags;
				break;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_SET_FLAG:
			{
				runtime_api_pipe_flags_t flags = va_arg(ap, runtime_api_pipe_flags_t);
				handle->pipe_flags |= flags;
				break;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_CLR_FLAG:
			{
				runtime_api_pipe_flags_t flags = va_arg(ap, runtime_api_pipe_flags_t);
				handle->pipe_flags &= ~flags;
				break;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_EOM:
			{
				const char* buf = va_arg(ap, const char*);
				size_t offset = va_arg(ap, size_t);
				_INVOKE_NULLABLE(int, ERROR_CODE(int), eom, buf, offset);
				return rc;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_PUSH_STATE:
			{
				void* state = va_arg(ap, void*);
				itc_module_state_dispose_func_t func = va_arg(ap, itc_module_state_dispose_func_t);

				const itc_modtab_instance_t* mod = _get_module((handle));

				if(mod == NULL || mod->module->push_state == NULL)
				{
					LOG_DEBUG("We do not support state preserving with the pipe type, just reject the submitted state");
					return 0;
				}

				int rc = 0;
				_INVOKE_MODULE(int, rc, mod, push_state, handle->data, state, func);

				if(ERROR_CODE(int) == rc)
				    ERROR_RETURN_LOG(int, "Cannot finish module call to push_state");

				return 1;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_POP_STATE:
			{
				void** buffer = va_arg(ap, void**);

				_GET_MODULE(mod, handle, 0);
				void *rc = NULL;
				*buffer = NULL;
				if(mod->module->pop_state == NULL) return 0;
				_INVOKE_MODULE(void*, rc, mod, pop_state, handle->data);

				*buffer = rc;

				return 0;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_READHDR:
			{
				void*  data = va_arg(ap, void*);
				size_t size = va_arg(ap, size_t);
				size_t* actual_size = va_arg(ap, size_t*);

				if(NULL == actual_size) ERROR_RETURN_LOG(int, "Invalid arguments");
				size_t rc = _read_header(data, size, handle);
				if(ERROR_CODE(size_t) == rc)
				    ERROR_RETURN_LOG(int, "Cannot read the typed header from pipe");

				*actual_size = rc;
				return 0;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_WRITEHDR:
			{
				const void* data = va_arg(ap, const void*);
				size_t      size = va_arg(ap, size_t);
				size_t* actual_size = va_arg(ap, size_t*);

				if(NULL == actual_size) ERROR_RETURN_LOG(int, "Invalid arguments");
				size_t rc = _write_header(data, size, handle);
				if(ERROR_CODE(size_t) == rc)
				    ERROR_RETURN_LOG(int, "Cannot write the typed header to pipe");

				*actual_size = rc;
				return 0;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_MODPATH:
			{
				char const**  buf = va_arg(ap, char const**);

				if(NULL == buf) ERROR_RETURN_LOG(int, "Invalid arguments");
				_GET_MODULE(mod, handle, ERROR_CODE(int));

				*buf = mod->path;
				return 0;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_GET_HDR_BUF:
			{
				size_t nbytes = va_arg(ap, size_t);
				void const** buf = va_arg(ap, void const**);

				int rc = _get_header_buf(buf, nbytes, handle);

				if(rc == ERROR_CODE(int))
					ERROR_RETURN_LOG(int, "Cannot get the header buffer from the pipe");

				if(rc == 0) *buf = NULL;

				return 0;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_GET_DATA_BUF:
			{
				size_t min_size = 0;
				size_t max_size = va_arg(ap, size_t);
				void const** buf = va_arg(ap, void const**);
				size_t* min_size_buf = va_arg(ap, size_t*);
				size_t* max_size_buf = va_arg(ap, size_t*);

				if(NULL == min_size_buf || NULL == max_size_buf || min_size_buf == max_size_buf)
					ERROR_RETURN_LOG(int, "Invalid arguments");

				int rc = _get_data_body_buf(buf, &min_size, &max_size, handle);

				if(rc == ERROR_CODE(int))
					ERROR_RETURN_LOG(int, "Cannot get the data body buffer from the pipe");

				if(rc == 0) 
					*buf = NULL;

				*min_size_buf = min_size;
				*max_size_buf = max_size;

				return 0;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_PUT_DATA_BUF:
			{
				const void* buf = va_arg(ap, const void*);
				size_t actual_size = va_arg(ap, size_t);

				if(NULL == buf) ERROR_RETURN_LOG(int, "Invalid arguments");

				int rc;
				
				_GET_MODULE(mod, handle, 0);

				_INVOKE_MODULE(int, rc, mod, release_internal_buf, buf, actual_size, handle->data);

				return rc;
			}
			case RUNTIME_API_PIPE_CNTL_OPCODE_NOP:
			{
				return 0;
			}
			default:
			    ERROR_RETURN_LOG(int, "Unknown opcode %x", opcode);
		}
	}
	else
	{
		const itc_modtab_instance_t* code_mod = _get_module_from_type(opcode_type);
		if(NULL == code_mod) ERROR_RETURN_LOG(int, "Invalid opcode %u", opcode_type);
		_GET_MODULE(mod, handle, 0);

		if(mod->module != code_mod->module)
		{
			LOG_DEBUG("Ignored pipe_cntl call to a different type module, expected type: %u, actual type: %u", opcode_type, handle->module_type);
			return 0;
		}

		int rc = 0;
		if(mod->module->cntl == NULL) return 0;
		_INVOKE_MODULE(int, rc, mod, cntl, handle->data, RUNTIME_API_PIPE_CNTL_OPCODE_MOD_SPEC(opcode), ap);
		if(rc == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot finish cntl call");

		return rc;
	}

	return 0;
}

int itc_module_loop_killed(itc_module_type_t module)
{
	const itc_modtab_instance_t* inst = _get_module_from_type(module);
	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Invalid module");

	const itc_module_t* mod = inst->module;
	void* context = inst->context;

	if(mod->event_thread_killed != NULL)
	    mod->event_thread_killed(context);

	return 0;
}

const char* itc_module_get_name(itc_module_type_t module, char* buffer, size_t size)
{
	static char default_buffer[1024];

	if(NULL == buffer)
	{
		buffer = default_buffer;
		size = sizeof(default_buffer);
	}


	const itc_modtab_instance_t* inst = _get_module_from_type(module);
	if(NULL == inst)
	    ERROR_PTR_RETURN_LOG("Invalid module");

	string_buffer_t sbuf;

	string_buffer_open(buffer, size, &sbuf);
	if(inst->module->get_path == NULL)
	    string_buffer_appendf(&sbuf, "Module_%x", module);
	else
	{
		string_buffer_appendf(&sbuf, "%s(Module_Id: %x)", inst->path, module);
	}
	return string_buffer_close(&sbuf);
}

const char* itc_module_get_path(itc_module_type_t module, char* buffer, size_t size)
{
	static char default_buffer[1024];

	if(NULL == buffer)
	{
		buffer = default_buffer;
		size = sizeof(default_buffer);
	}

	const itc_modtab_instance_t* inst = _get_module_from_type(module);

	if(NULL == inst)
	    ERROR_PTR_RETURN_LOG("Invalid module ID %x", module);

	if(inst->module->get_path == NULL)
	    ERROR_PTR_RETURN_LOG("Path to the module instance is undefined");

	string_buffer_t sbuf;
	string_buffer_open(buffer, size, &sbuf);
	string_buffer_append(inst->path, &sbuf);

	return string_buffer_close(&sbuf);
}

int itc_module_is_pipe_shadow(const itc_module_pipe_t* handle)
{
	if(NULL == handle) ERROR_RETURN_LOG(int, "Invalid arguments");
	return (handle->pipe_flags & RUNTIME_API_PIPE_SHADOW) != 0;
}

int itc_module_is_pipe_input(const itc_module_pipe_t* handle)
{
	if(NULL == handle) ERROR_RETURN_LOG(int, "Invalid arguments");
	return handle->stat.type == _PSTAT_TYPE_INPUT;
}

int itc_module_is_pipe_cancelled(const itc_module_pipe_t* handle)
{
	if(NULL == handle) ERROR_RETURN_LOG(int, "Invalid arguments");
	return handle->stat.i_canclled;
}

itc_module_pipe_t* itc_module_pipe_fork(itc_module_pipe_t* handle, runtime_api_pipe_flags_t pipe_flags, size_t header_size, const void* args)
{
	if(NULL == handle) ERROR_PTR_RETURN_LOG("Invalid arguments");

	itc_module_pipe_t* source_handle = handle;
	if(NULL == source_handle) ERROR_PTR_RETURN_LOG("Invalid source handle");

	const itc_modtab_instance_t* inst;
	if((inst = _get_module(source_handle)) == NULL)
	    ERROR_PTR_RETURN_LOG("Invalid source handle");

	itc_module_pipe_t* ret = (itc_module_pipe_t*)mempool_objpool_alloc(inst->handle_pool);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the forked pipe");

	ret->owner = NULL;
	ret->module_type = source_handle->module_type;

	ret->companion_next = source_handle->companion_next;
	ret->companion_prev = source_handle;
	ret->companion_prev->companion_next = ret;
	ret->companion_next->companion_prev = ret;

	ret->stat_flags = 0;
	ret->stat.type = _PSTAT_TYPE_INPUT;
	ret->pipe_flags = pipe_flags;
	ret->stat.accepted = 0;
	ret->stat.error = 0;

	ret->stat.s_hold = 1;

	/* Setup the type information */
	ret->actual_header_size = source_handle->actual_header_size;
	ret->expected_header_size = header_size;
	ret->processed_header_size = 0;

	const itc_module_t* module = inst->module;
	void* context = inst->context;

	ret->stat.i_canclled = handle->stat.i_canclled;

	if(module->fork == NULL)
	    ERROR_LOG_GOTO(ERR, "Module doesn't support fork operation");
	else if(ERROR_CODE(int) == module->fork(context, ret->data, source_handle->data, args))
	    ERROR_LOG_GOTO(ERR, "Module function fork retuend with an error code");

	return ret;
ERR:
	mempool_objpool_dealloc(inst->handle_pool, ret);
	return NULL;
}

void* itc_module_get_context(itc_module_type_t type)
{
	const itc_modtab_instance_t* inst;
	if((inst = _get_module_from_type(type)) == NULL)
	    ERROR_PTR_RETURN_LOG("Invalid source handle");

	return inst->context;
}

int itc_module_on_exit(itc_module_type_t module)
{
	const itc_modtab_instance_t* inst;
	if((inst = _get_module_from_type(module)) == NULL)
	    ERROR_RETURN_LOG(int, "Invalid target module");

	if(inst->module->on_exit == NULL)
	    LOG_DEBUG("Ignore the on exit module call for the module %s, because it's not defined by the module", inst->path);
	else if(ERROR_CODE(int) == inst->module->on_exit(inst->context))
	    ERROR_RETURN_LOG(int, "The on exit module call for module instance %s returns an error code", inst->path);
	else
	    LOG_DEBUG("The on exit module call for module instance %s returns successfully", inst->path);

	return 0;
}

int itc_module_pipe_set_error(itc_module_pipe_t* handle)
{
	if(NULL == handle) ERROR_RETURN_LOG(int, "Invalid arguments");

	handle->stat.error = 1;

	return 0;
}

int itc_module_pipe_is_touched(const itc_module_pipe_t* handle)
{
	if(NULL == handle)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if((handle->pipe_flags & RUNTIME_API_PIPE_SHADOW) > 0)
	   return !(handle->pipe_flags & RUNTIME_API_PIPE_DISABLED);

	if(handle->stat.type != _PSTAT_TYPE_OUTPUT)
	    ERROR_RETURN_LOG(int, "Wrong pipe types, expected output, got input");

	return handle->stat.o_touched && !handle->stat.error;
}
