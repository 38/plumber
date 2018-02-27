/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <error.h>
#include <pservlet.h>
#include <pstd/scope.h>

static inline pipe_t _ensure_pipe(pipe_t current, const char* func)
{
	pipe_t ret = current;
	if(ERROR_CODE(pipe_t) == ret)
	{
		if(ERROR_CODE(pipe_t) == (ret = module_require_function("plumber.std", func)))
		    ERROR_RETURN_LOG(pipe_t, "Cannot find servlet module function plumber.std.%s make sure you have installed pssm module", func);
	}

	return ret;
}

#define _ENSURE_PIPE(name, retval) \
    static pipe_t name = ERROR_CODE(pipe_t);\
    if(ERROR_CODE(pipe_t) == (name = _ensure_pipe(name, #name))) \
        return retval;

scope_token_t pstd_scope_add(const scope_entity_t* entity)
{
	_ENSURE_PIPE(scope_add, ERROR_CODE(scope_token_t));

	scope_token_t ret;

	if(ERROR_CODE(int) == pipe_cntl(scope_add, PIPE_CNTL_INVOKE, entity, &ret))
	    ERROR_RETURN_LOG(scope_token_t, "Cannot finish the pipe_cntl call");

	return ret;
}

scope_token_t pstd_scope_copy(scope_token_t token, void** resbuf)
{
	_ENSURE_PIPE(scope_copy, ERROR_CODE(scope_token_t));

	scope_token_t ret;

	if(ERROR_CODE(int) == pipe_cntl(scope_copy, PIPE_CNTL_INVOKE, token, &ret, resbuf))
	    ERROR_RETURN_LOG(scope_token_t, "Cannot finish the pipe_cntl call");

	return ret;
}

const void* pstd_scope_get(scope_token_t token)
{
	_ENSURE_PIPE(scope_get, NULL);

	const void* ret;

	if(ERROR_CODE(int) == pipe_cntl(scope_get, PIPE_CNTL_INVOKE, token, &ret))
	    ERROR_PTR_RETURN_LOG("Cannot finish the pipe_cntl call");

	return ret;
}

pstd_scope_stream_t* pstd_scope_stream_open(scope_token_t token)
{
	_ENSURE_PIPE(scope_stream_open, NULL);

	void* ret = NULL;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_open, PIPE_CNTL_INVOKE, token, &ret))
		ERROR_PTR_RETURN_LOG("Cannot finish the pipe_cntl call");

	return (pstd_scope_stream_t*)ret;
}

size_t pstd_scope_stream_read(pstd_scope_stream_t* stream, void* buf, size_t size)
{
	_ENSURE_PIPE(scope_stream_read, ERROR_CODE(size_t));

	size_t ret = 0;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_read, PIPE_CNTL_INVOKE, stream, buf, size, &ret))
		ERROR_RETURN_LOG(size_t, "Cannot finish the pipe_cntl call");

	return ret;
}

int pstd_scope_stream_eof(const pstd_scope_stream_t* stream)
{
	_ENSURE_PIPE(scope_stream_eof, ERROR_CODE(int));

	int ret = 0;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_eof, PIPE_CNTL_INVOKE, stream, &ret))
		ERROR_RETURN_LOG(int, "Cannot finish the pipe_cntl call");

	return ret;
}

int pstd_scope_stream_close(pstd_scope_stream_t* stream)
{
	_ENSURE_PIPE(scope_stream_close, ERROR_CODE(int));

	return pipe_cntl(scope_stream_close, PIPE_CNTL_INVOKE, stream);
}

int pstd_scope_stream_ready_event(pstd_scope_stream_t* stream, scope_ready_event_t* buf)
{
	_ENSURE_PIPE(scope_stream_ready_event, ERROR_CODE(int));

	int ret = 0;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_ready_event, PIPE_CNTL_INVOKE, stream, buf, &ret))
		ERROR_RETURN_LOG(int, "Cannot finish the pipe_cntl call");

	return ret;
}
