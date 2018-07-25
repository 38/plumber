/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <error.h>
#include <stdlib.h>

typedef struct {
	int id;
	pipe_array_t* inputs;
	pipe_t output;
} context_t;


static int init(uint32_t argc, char const* const* argv, void* mem)
{
	(void) argc;

	context_t* ctx = (context_t*)mem;

	ctx->id = atoi(argv[1]);
	ctx->inputs = pipe_array_new("in#", PIPE_INPUT, NULL, 0, atoi(argv[2]));
	ctx->output = pipe_define("out", PIPE_OUTPUT, NULL);

	return 0;
}

static int exec(void* mem)
{
	context_t* ctx = (context_t*)mem;

	uint32_t i, sum = 0;
	for(i = 0; i < ctx->inputs->size; i ++)
	{
		uint32_t buf;
		if(pipe_read(pipe_array_get(ctx->inputs, i), &buf, sizeof(uint32_t)) > 0)
			sum += buf;
	}
	pipe_write(ctx->output, &sum, sizeof(uint32_t));

	trap(ctx->id);

	return 0;
}

static int cleanup(void* mem)
{
	context_t* ctx = (context_t*)mem;
	pipe_array_free(ctx->inputs);

	return 0;
}

SERVLET_DEF = {
	.desc = "The multi-way concatenation test",
	.version = 0,
	.size = sizeof(context_t),
	.init = init,
	.exec = exec,
	.unload = cleanup
};
