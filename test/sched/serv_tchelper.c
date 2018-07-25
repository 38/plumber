/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <error.h>
#include <stdlib.h>
typedef struct {
	pipe_t input;
	pipe_t dummy;
	pipe_t output[2];
	int which;
	int id;
} data_t;

static int init(uint32_t argc, char const* const* argv, void* mem)
{
	(void) argc;
	data_t* data = (data_t*)mem;

	data->input = pipe_define("i0", PIPE_INPUT, NULL);
	data->dummy = ERROR_CODE(pipe_t);
	data->output[0] = pipe_define("o0", PIPE_OUTPUT, NULL);
	data->output[1] = pipe_define("o1", PIPE_OUTPUT, NULL);

	if(data->input == ERROR_CODE(pipe_t) ||
	   data->output[0] == ERROR_CODE(pipe_t) ||
	   data->output[1] == ERROR_CODE(pipe_t))
	{
		LOG_ERROR("cannot define pipe");
		return ERROR_CODE(int);
	}

	data->which = atoi(argv[2]);
	data->id = atoi(argv[1]);
	if(data->which == 4)
	{
		data->dummy = pipe_define("i1", PIPE_INPUT, NULL);
		data->which = 1;
	}
	return 0;
}

static int exec(void* mem)
{
	char buffer[1024];
	data_t* data = (data_t*) mem;

	size_t sz = pipe_read(data->input, buffer, sizeof(buffer));

	if(sz == ERROR_CODE(size_t)) return ERROR_CODE(int);

	if((data->which & 1) && pipe_write(data->output[0], buffer, sz) == ERROR_CODE(size_t))
		return ERROR_CODE(int);
	if((data->which & 2) && pipe_write(data->output[1], buffer, sz) == ERROR_CODE(size_t))
		return ERROR_CODE(int);

	trap(data->id);
	return 0;
}

static int unload(void* mem)
{
	(void) mem;
	return 0;
}

SERVLET_DEF = {
	.desc = "Task Cancellation Test Helper",
	.version = 0,
	.size = sizeof(data_t),
	.init = init,
	.exec = exec,
	.unload = unload
};
