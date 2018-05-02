/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>
#include <psnl.h>

#include <pstd/types/string.h>

#include <options.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	pipe_t             in;           /*!< The input text */ 
	pipe_t             out;          /*!< The output field */
	pstd_type_model_t* type_model;   /*!< The type model for current servlet */
	options_t          options;      /*!< The servlet option */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the options");

	return 0;
}

SERVLET_DEF = {
	.desc = "The parser to parse a initial field configuration",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init
};
