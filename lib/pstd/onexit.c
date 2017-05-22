/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <error.h>

#include <pservlet.h>
#include <pstd/onexit.h>

int pstd_onexit(pstd_onexit_callback_t callback, void* data)
{
	if(NULL == callback)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	static pipe_t pipe = ERROR_CODE(pipe_t);

	if(ERROR_CODE(pipe_t) == pipe && ERROR_CODE(pipe_t) == (pipe = module_require_function("plumber.std", "on_exit")))
	    ERROR_RETURN_LOG(int, "Could not find service moudle function plumber.std.on_exit, is PSSM module loaded?");

	return pipe_cntl(pipe, PIPE_CNTL_INVOKE, callback, data);
}
