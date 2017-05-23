/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <pservlet.h>
#include <pstd.h>
/**
 * @brief The servlet context
 **/
typedef struct {
	pipe_t          input;   /*!< The input pipe */
	pipe_array_t*   outputs;  /*!< The output pipes */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	if(argc != 2)
	    ERROR_RETURN_LOG(int, "Usage: %s <num-of-outputs>", argv[0]);

	int size = atoi(argv[1]);

	if(ERROR_CODE(pipe_t) == (ctx->input = pipe_define("in", PIPE_INPUT, "$T")))
	    ERROR_RETURN_LOG(int, "Cannot define the input pipe");

	if(NULL == (ctx->outputs = pipe_array_new("out#", PIPE_MAKE_SHADOW(ctx->input), "$T", 0, size)))
	    ERROR_RETURN_LOG(int, "Cannot create outputs");

	return 0;
}

static int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	if(NULL != ctx->outputs && ERROR_CODE(int) == pipe_array_free(ctx->outputs))
	    ERROR_RETURN_LOG(int, "Cannot dispose the output pipes");
	return 0;
}

static int _exec(void* ctxbuf)
{
	(void)ctxbuf;
	return 0;
}

SERVLET_DEF = {
	.desc = "The duplicator duplicates a single input to multiple output",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
