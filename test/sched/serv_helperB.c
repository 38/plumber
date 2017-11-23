/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <stdio.h>
#include <error.h>
static pipe_t in1, in2, out, err;

static int init(uint32_t argc, char const* const* argv, void* data)
{
	(void) argc;
	(void) argv;

	in1 = pipe_define("stdin1", PIPE_INPUT, NULL);
	in2 = pipe_define("stdin2", PIPE_INPUT, NULL);
	out = pipe_define("stdout", PIPE_OUTPUT, NULL);
	err = pipe_define("stderr", PIPE_OUTPUT, NULL);
	if(in1 == ERROR_CODE(pipe_t) || in2 == ERROR_CODE(pipe_t) || out == ERROR_CODE(pipe_t) || err == ERROR_CODE(pipe_t))
	{
		LOG_ERROR("cannot define pipes");
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
	(void) args;
	int a, b, c;
	pipe_read(in1, &a, sizeof(int));
	pipe_read(in2, &b, sizeof(int));
	c = b - a;
	LOG_INFO("Input = <%d, %d>, Output = %d", a, b, c);
	pipe_write(out, &c, sizeof(int));
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
