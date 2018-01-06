/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <curl/curl.h>

#include <client.h>

typedef struct {
	pipe_t req_data_p; /*!< The request data */
} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* data)
{
	(void)argc;
	(void)argv;

	ctx_t* ctx = (ctx_t*)data;
	ctx->req_data_p = pipe_define("request", PIPE_INPUT, NULL);

	if(ERROR_CODE(int) == client_init(1024, 128, 4))
		ERROR_RETURN_LOG(int, "Cannot intialize the client library");

	return 1;
}

static int _cleanup(void* data)
{
	(void)data;
	if(ERROR_CODE(int) == client_finalize())
		ERROR_RETURN_LOG(int, "Cannot finalize the client library");

	return 0;
}

static int _async_setup(async_handle_t* handle, void* data, void* ctxbuf)
{
	(void)handle;
	(void)data;
	(void)ctxbuf;

	client_add_request("test", handle, 0, 0, NULL, NULL);

	/* TODO: try to do non-blocking request posting */

	return 0;
}

static int _async_cleanup(async_handle_t* handle, void* data, void* ctxbuf)
{
	(void)handle;
	(void)data;
	(void)ctxbuf;

	/* TODO: construct the request result */

	return 0;
}

static int _async_exec(async_handle_t* handle, void* data)
{
	(void)handle;
	(void)data;

	/* TODO: blocking request posting */

	return 0;
}

SERVLET_DEF = {
	.size = sizeof(ctx_t),
	.async_buf_size = sizeof(int),
	.desc = "The HTTP client servlet",
	.version = 0x0,
	.init = _init,
	.unload = _cleanup,
	.async_setup = _async_setup,
	.async_cleanup = _async_cleanup,
	.async_exec = _async_exec
};
