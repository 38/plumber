/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include <error.h>

#include <pservlet.h>
#include <utils/log.h>
#include <pstd/libconf.h>

static inline pipe_t _ensure_pipe(void)
{
	static pipe_t ret = ERROR_CODE(pipe_t);
	if(ERROR_CODE(pipe_t) == ret)
	{
		if(ERROR_CODE(pipe_t) == (ret = module_require_function("plumber.std", "get_libconfig")))
			ERROR_RETURN_LOG(pipe_t, "Cannot find servlet module function plumber.std.get_libconfig make sure you have installed pssm module");
	}

	if(ERROR_CODE(pipe_t) == ret)
		LOG_WARNING("Cannot get the pipe for the service module function plumber.std.get_libconfig");

	return ret;
}


int64_t pstd_libconf_read_numeric(const char* key, int64_t default_val)
{
	if(NULL == key) return default_val;

	pipe_t pipe = _ensure_pipe();

	if(ERROR_CODE(pipe_t) == pipe) return default_val;

	int is_num = 0;
	const void* result = NULL;

	if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_INVOKE, key, &is_num, &result))
	{
		LOG_WARNING("Cannot invoke the service module function plumber.std.get_libconfig");
		return default_val;
	}

	if(!is_num || result == NULL) return default_val;

	return *(const int64_t*)result;
}

const char* pstd_libconf_read_string(const char* key, const char* default_val)
{
	if(NULL == key) return default_val;

	pipe_t pipe = _ensure_pipe();

	if(ERROR_CODE(pipe_t) == pipe) return default_val;

	int is_num = 0;
	const void* result = NULL;
	if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_INVOKE, key, &is_num, &result))
	{
		LOG_WARNING("Cannot invoke the service module function p lumber.std.egt_libconfig");
		return default_val;
	}

	if(is_num || result == NULL) return default_val;

	return (const char*)result;
}
