/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

typedef struct {
	pipe_t req_data_p; /*!< The request data */
} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* data)
{
	(void)argc;
	(void)argv;

	ctx_t* ctx = (ctx_t*)data;
	ctx->req_data_p = pipe_define("request", PIPE_INPUT, NULL);

	return 0;
}

SERVLET_DEF = {
	.size = sizeof(ctx_t),
	.desc = "The HTTP client servlet",
	.version = 0x0,
	.init = _init
};
