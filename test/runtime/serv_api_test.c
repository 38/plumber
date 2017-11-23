/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <string.h>
#include <pservlet.h>
#include <stdlib.h>
#include <stdio.h>
#include <error.h>
#define N 128
static void* data;
uint32_t opcode;
static int init(uint32_t argc, char const* const* argv, void* _data)
{
	(void) argc;
	(void) argv;
	data = _data;
	int* result = (int*)data;
	result[0] = N;
	int i;
	for(i = 0; i < N; i ++)
	{
		static char buf[128];
		snprintf(buf, sizeof(buf), "test%d", i);
		result[i + 1] = RUNTIME_API_PIPE_TO_PID(pipe_define(buf, (pipe_flags_t)(i * 3), NULL));
	}
	trap(0);

	result[0] = RUNTIME_API_PIPE_TO_PID(pipe_define("test0", 0, NULL));
	trap(1);

	opcode = module_get_opcode("pipe.test.test", 0x0);

	return 0;
}

static void _trap(int n)
{
	trap(n);
}
#define _(id) RUNTIME_API_PIPE_FROM_ID(id)
static int exec(void* args)
{
	(void) args;
	LOG_NOTICE("start a exec task!");
	const char* text = "this is a read write test!";
	char buf[128] = {};
	const char** result = (const char**) data;
	if((size_t)pipe_write(_(0), text, strlen(text) + 1) == strlen(text) + 1 &&
	   pipe_read(_(1), buf, strlen(text) + 1) == strlen(text) + 1)
	{
		LOG_NOTICE("%s", buf);
		result[0] = text;
		result[1] = buf;
		trap(2);
	}

	result[0] = text;
	static char rdbuf[1024];

	if((size_t)pipe_write(_(0), text, strlen(text) + 1) == strlen(text) + 1 &&
	   pipe_read(_(1), rdbuf, strlen(text) + 1) == strlen(text) + 1)
	{
		trap(3);
	}

	LOG_DEBUG("Test EOF API");
	int i;
	char tmp;
	int* rcs = (int*) data;
	rcs[0] = 0;
	for(i = 0; i < 4096 && !rcs[0]; i ++)
	{
		if(pipe_read(_(3), &tmp, 1) == ERROR_CODE(size_t)) rcs[0] = -1;
		if(pipe_eof(_(3)) != 0) rcs[0] = -1;
	}

	rcs[1] = -1;
	for(i = 0; i < 4096 + 100; i ++)
	{
		size_t r = pipe_read(_(5), &tmp, 1);
		if(r == ERROR_CODE(size_t))
		{
			rcs[1] = -1;
			break;
		}
		if(r == 0)
		{
			if(pipe_eof(_(5)) > 0) rcs[1] = 0;
			else rcs[1] = -1;
		}
		else
		{
			if(pipe_eof(_(3)) != 0)
			{
				rcs[1] = -1;
				break;
			}
		}
	}

	trap(4);

	/* Test the pipe cntl */
	if(pipe_cntl(_(3), opcode, _trap) == ERROR_CODE(int)) return -1;
	pipe_flags_t pf;
	if(pipe_cntl(_(3), PIPE_CNTL_GET_FLAGS, &pf) == ERROR_CODE(int)) return -1;
	if(pf != PIPE_INPUT) return -1;
	if(pipe_cntl(_(3), PIPE_CNTL_SET_FLAG, PIPE_PERSIST) == ERROR_CODE(int)) return -1;
	if(pipe_cntl(_(3), PIPE_CNTL_GET_FLAGS, &pf) == ERROR_CODE(int)) return -1;
	if(pf != (PIPE_INPUT | PIPE_PERSIST)) return -1;
	if(pipe_cntl(_(3), opcode, _trap) == ERROR_CODE(int)) return -1;
	if(pipe_cntl(_(3), PIPE_CNTL_CLR_FLAG, PIPE_PERSIST) == ERROR_CODE(int)) return -1;
	if(pipe_cntl(_(3), PIPE_CNTL_GET_FLAGS, &pf) == ERROR_CODE(int)) return -1;
	if(pf != PIPE_INPUT) return -1;

	if(pipe_cntl(_(6), PIPE_CNTL_SET_FLAG, PIPE_PERSIST) == ERROR_CODE(int)) return -1;
	if(pipe_cntl(_(6), PIPE_CNTL_GET_FLAGS, &pf) == ERROR_CODE(int)) return -1;
	if(pf != (PIPE_OUTPUT | PIPE_PERSIST)) return -1;

	trap(5);

	return 0;
}

SERVLET_DEF = {
	.size = sizeof(int) * (N + 1),
	.desc = "API Testing servlet",
	.version = 0,
	.init = init,
	.exec = exec
};
