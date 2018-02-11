/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>

#include <options.h>
#include <connection.h>

typedef struct {
	options_t options;
	pipe_t    request;
	pipe_t    response;
} _ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	_ctx_t* ctx = (_ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Invalid servlete initialization string");

	if(ERROR_CODE(pipe_t) == (ctx->request = pipe_define("request", PIPE_INPUT, "plumber/std_servlet/network/http/proxy/v0/Request")))
		ERROR_RETURN_LOG(int, "Cannot define the request pipe");

	if(ERROR_CODE(pipe_t) == (ctx->response = pipe_define("response", PIPE_OUTPUT, "plumber/std_servlet/network/http/proxy/v0/Response")))
		ERROR_RETURN_LOG(int, "Cannot define the response pipe");

	if(ERROR_CODE(int) == connection_pool_init(ctx->options.conn_pool_size, ctx->options.conn_per_peer))
		ERROR_RETURN_LOG(int, "Cannot initialize the connection pool for this servlet instance");

	return 0;
}

static int _cleanup(void* ctxmem)
{
	_ctx_t* ctx = (_ctx_t*)ctxmem;
	(void)ctx;

	if(ERROR_CODE(int) == connection_pool_finalize())
		ERROR_RETURN_LOG(int, "Cannot finalize the connection pool for this servlet instance");

	return 0;
}

SERVLET_DEF = {
	.desc    = "The HTTP Proxy Servlet",
	.version = 0x0,
	.size    = sizeof(_ctx_t),
	.init    = _init,
	.unload  = _cleanup
};
