/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <trie.h>
#include <routing.h>
#include <options.h>

typedef struct {
	options_t          options;     /*!< The options */
	pstd_type_model_t* type_model;  /*!< The type model */
} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the options");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create type model for the servlet");

	if(ERROR_CODE(int) == routing_map_initialize(ctx->options.routing_map, ctx->type_model))
		ERROR_RETURN_LOG(int, "Cannot initailize the routing map");

	return 0;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;
	if(ERROR_CODE(int) == options_free(&ctx->options))
		rc = ERROR_CODE(int);

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);

	return rc;
}

SERVLET_DEF = {
	.desc    = "The HTTP Request Parser",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _unload
};
