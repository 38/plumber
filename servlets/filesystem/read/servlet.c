/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t opts;    /*!< The servlet options */

	pipe_t    p_path;  /*!< The input pipe for the path to the file we want to read */
	pipe_t    o_result;/*!< The output pipe for the result read from disk */

} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->opts))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	return 0;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc;

	if(ERROR_CODE(int) == options_free(&ctx->opts))
		rc = ERROR_CODE(int);

	return rc;
}

static int _exec(void* ctxmem)
{
	(void)ctxmem;

	return 0;
}

SERVLET_DEF = {
	.desc    = "Read file from disk",
	.size    = sizeof(ctx_t),
	.version = 0x0,
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};
