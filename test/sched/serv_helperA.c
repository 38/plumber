/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <stdio.h>
#include <error.h>
static pipe_t in, out, err;
static int init(uint32_t argc, char const* const* argv, void* data)
{
	(void) argc;
	(void) argv;
	in = pipe_define("stdin", PIPE_INPUT, NULL);
	out = pipe_define("stdout", PIPE_OUTPUT, NULL);
	err = pipe_define("stderr", PIPE_OUTPUT, NULL);
	if(in == ERROR_CODE(pipe_t)  || out == ERROR_CODE(pipe_t) || err == ERROR_CODE(pipe_t))
	{
		LOG_ERROR("can not define pipes");
		return ERROR_CODE(int);
	}
	sscanf(argv[1], "%d", (int*)data);
	LOG_INFO("Test Helper A has beed loaded with argument %d", *(int*)data);
	return 0;
}

static int cleanup(void* data)
{
	(void) data;
	LOG_INFO("Test Helper A is unloaded");
	return 0;
}

static int exec(void* args)
{
	trap(*(int*)args);
	int num, res[2], k = *(int*)args;
	pipe_read(in, &num, sizeof(int));
	res[0] = num * (k + 1);
	res[1] = num * (k + 2);
	LOG_INFO("Input = %d, Output = <%d, %d>", num, res[0], res[1]);
	pipe_write(out, res + 0, sizeof(int));
	pipe_write(err, res + 1, sizeof(int));
	return 0;
}

SERVLET_DEF = {
	.size    = sizeof(int),
	.desc    = "Scheduler Test Helper A",
	.version = 0,
	.init    = init,
	.unload  = cleanup,
	.exec    = exec
};
