/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>

typedef struct {
	pipe_t request;
	pipe_t response;
} _ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	(void)argc;
	(void)argv;
	_ctx_t* ctx = (_ctx_t*)ctxmem;

	if(ERROR_CODE(pipe_t) == (ctx->request = pipe_define("request", PIPE_INPUT, "plumber/std_servlet/network/http/proxy/v0/Request")))
		ERROR_RETURN_LOG(int, "Cannot define the request pipe");

	if(ERROR_CODE(pipe_t) == (ctx->response = pipe_define("response", PIPE_OUTPUT, "plumber/std_servlet/network/http/proxy/v0/Response")))
		ERROR_RETURN_LOG(int, "Cannot define the response pipe");

	return 0;
}

SERVLET_DEF = {
	.desc    = NULL,
	.version = 0x0,
	.size    = sizeof(_ctx_t),
	.init    = _init
};
