/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <error.h>
#include <pservlet.h>
#include <thread.h>
#define _THREAD_LOCAL_MAGIC ((uintptr_t)0x544c4d6167696321ull)
struct _pstd_thread_local_t {
	uintptr_t	magic;     /*!< the magic number used to identify this is a thread local object */
	void*       object;    /*!< the reference to the actual object */
};

static inline pipe_t _ensure_pipe(pipe_t current, const char* func)
{
	pipe_t ret = current;
	if(current == ERROR_CODE(pipe_t))
	{
		ret = module_require_function("plumber.std", func);

		if(ERROR_CODE(pipe_t) == ret)
		    ERROR_RETURN_LOG(pipe_t, "Cannot get the service module function plumber.std.%s, make sure PSSM is installed", func);
	}

	return ret;
}

#define _ENSURE_PIPE(name, retval) \
    static pipe_t name = ERROR_CODE(pipe_t);\
    if(ERROR_CODE(pipe_t) == (name = _ensure_pipe(name, #name))) \
        return retval;

pstd_thread_local_t* pstd_thread_local_new(pstd_thread_local_allocator_t alloc, pstd_thread_local_dealloctor_t dealloc, const void* data)
{
	_ENSURE_PIPE(thread_local_new, NULL);
	pstd_thread_local_t* ret = (pstd_thread_local_t*)malloc(sizeof(pstd_thread_local_t));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the thread local object");
	ret->magic =  _THREAD_LOCAL_MAGIC;

	if(ERROR_CODE(int) == pipe_cntl(thread_local_new, PIPE_CNTL_INVOKE, alloc, dealloc, data, &ret->object))
	    ERROR_LOG_GOTO(ERR, "Call to plumber.std.thread_local_new has failed");

	return ret;
ERR:
	free(ret);
	return NULL;
}


void* pstd_thread_local_get(pstd_thread_local_t* local)
{
	if(local == NULL || local->magic != _THREAD_LOCAL_MAGIC)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	_ENSURE_PIPE(thread_local_get, NULL);

	void* ret;

	if(ERROR_CODE(int) == pipe_cntl(thread_local_get, PIPE_CNTL_INVOKE, local->object, &ret))
	    ERROR_PTR_RETURN_LOG("Call to plumber.std.thread_local_get has failed");

	return ret;
}

int pstd_thread_local_free(pstd_thread_local_t* local)
{
	if(local == NULL || local->magic != _THREAD_LOCAL_MAGIC)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_ENSURE_PIPE(thread_local_free, ERROR_CODE(int));

	int rc = pipe_cntl(thread_local_free, PIPE_CNTL_INVOKE, local->object);

	if(rc == ERROR_CODE(int)) LOG_ERROR("Call to plumber.std.thread_local_free has failed");

	free(local);
	return rc;
}
