/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <stdlib.h>
static pipe_t test_pipe[3];
static int init(uint32_t argc, char const* const* argv,void* data)
{
	(void) argc;
	(void) argv;
	(void) data;
	/* Try to allocate one bytes, if something wrong with the memory we should catch that */
	*(void**)data = malloc(1);
	test_pipe[0] = pipe_define("test_pipe_0", PIPE_INPUT, NULL);
	test_pipe[1] = pipe_define("test_pipe_1", PIPE_OUTPUT, NULL);
	test_pipe[2] = pipe_define("test_pipe_2", PIPE_INPUT, NULL);
	LOG_DEBUG("Hello Plumber! pipe list: [%d, %d, %d]", test_pipe[0], test_pipe[1], test_pipe[2]);
	return 0;
}

static int unload(void* data)
{
	LOG_DEBUG("Goodbye Plumber!");
	free(*(void**)data);
	return 0;
}

SERVLET_DEF = {
	.size = sizeof(void*),
	.desc = "Test Servlet",
	.version = 0,
	.init = init,
	.unload = unload
};
