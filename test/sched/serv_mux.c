/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <error.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int id;
	pipe_t data;
	pipe_array_t* outputs;
} context_t;


static int init(uint32_t argc, char const* const* argv, void* mem)
{
	(void) argc;

	context_t* ctx = (context_t*)mem;

	ctx->id = atoi(argv[1]);
	ctx->data = pipe_define("data", PIPE_INPUT, NULL);
	ctx->outputs = pipe_array_new("out#", PIPE_OUTPUT | PIPE_SHADOW | PIPE_DISABLED | PIPE_GET_ID(ctx->data), NULL, 0, atoi(argv[2]));

	return 0;
}

static int exec(void* mem)
{
	context_t* ctx = (context_t*)mem;

	if(ERROR_CODE(int) == pipe_cntl(pipe_array_get(ctx->outputs, 0), PIPE_CNTL_CLR_FLAG, PIPE_DISABLED))
	    return ERROR_CODE(int);

	trap(ctx->id);

	return 0;
}

static int cleanup(void* mem)
{
	context_t* ctx = (context_t*)mem;
	pipe_array_free(ctx->outputs);

	return 0;
}

SERVLET_DEF = {
	.desc = "The multi-way selector test servlet",
	.version = 0,
	.size = sizeof(context_t),
	.init = init,
	.exec = exec,
	.unload = cleanup
};
