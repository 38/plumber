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
#include <proto.h>

#include <trie.h>
#include <routing.h>
#include <options.h>

typedef struct {
	pipe_t             p_input;     /*!< The input pipe for raw request */
	options_t          options;     /*!< The options */
	pstd_type_model_t* type_model;  /*!< The type model */

	uint32_t           METHOD_GET;  /*!< The method code for GET */
	uint32_t           METHOD_POST; /*!< The method code for GET */
	uint32_t           METHOD_HEAD; /*!< The method code for GET */

	uint64_t           RANGE_SEEK_SET;  /*!< The constant used to represent the head of the file */
	uint64_t           RANGE_SEEK_END;  /*!< THe constant used to represent the tail of the file */
} ctx_t;

static inline int _read_const_u32(const char* field, uint32_t* result)
{
	proto_db_field_prop_t prop = proto_db_field_type_info("plumber/std_servlet/network/http/parser/v0/RequestData", field);

	if(ERROR_CODE(proto_db_field_prop_t) == prop)
		ERROR_RETURN_LOG(int, "Cannot read the constant field");

	if((prop & PROTO_DB_FIELD_PROP_NUMERIC) && !(prop & PROTO_DB_FIELD_PROP_REAL) && !(prop & PROTO_DB_FIELD_PROP_REAL))
	{
		const void* default_data;
		size_t default_data_size;

		if(proto_db_field_get_default("plumber/std_servlet/network/http/parser/v0/RequestData", field, &default_data, &default_data_size) == ERROR_CODE(int))
			ERROR_RETURN_LOG(int, "Cannot read the value from the constant field");
		if(default_data_size != sizeof(uint32_t))
			ERROR_RETURN_LOG(int, "Invalid constant size");

		*result = *(const uint32_t*)default_data;
	}

	return 0;
}

static inline int _read_const_u64(const char* field, uint64_t* result)
{
	proto_db_field_prop_t prop = proto_db_field_type_info("plumber/std_servlet/network/http/parser/v0/RequestData", field);

	if(ERROR_CODE(proto_db_field_prop_t) == prop)
		ERROR_RETURN_LOG(int, "Cannot read the constant field");

	if((prop & PROTO_DB_FIELD_PROP_NUMERIC) && !(prop & PROTO_DB_FIELD_PROP_REAL) && !(prop & PROTO_DB_FIELD_PROP_REAL))
	{
		const void* default_data;
		size_t default_data_size;

		if(proto_db_field_get_default("plumber/std_servlet/network/http/parser/v0/RequestData", field, &default_data, &default_data_size) == ERROR_CODE(int))
			ERROR_RETURN_LOG(int, "Cannot read the value from the constant field");
		if(default_data_size != sizeof(uint64_t))
			ERROR_RETURN_LOG(int, "Invalid constant size");

		*result = *(const uint64_t*)default_data;
	}

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;
	
	memset(ctx, 0, sizeof(ctx_t));

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the options");

	if(ERROR_CODE(pipe_t) == (ctx->p_input = pipe_define("input", PIPE_INPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the input pipe");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create type model for the servlet");

	if(ERROR_CODE(int) == routing_map_initialize(ctx->options.routing_map, ctx->type_model))
		ERROR_RETURN_LOG(int, "Cannot initailize the routing map");

	if(ERROR_CODE(int) == proto_init())
		ERROR_RETURN_LOG(int, "Cannot initialize libproto");

	if(ERROR_CODE(int) == _read_const_u32("METHOD_GET", &ctx->METHOD_GET))
		ERROR_LOG_GOTO(ERR, "Cannot read constant for GET method");
	
	if(ERROR_CODE(int) == _read_const_u32("METHOD_POST", &ctx->METHOD_POST))
		ERROR_LOG_GOTO(ERR, "Cannot read constant for POST method");
	
	if(ERROR_CODE(int) == _read_const_u32("METHOD_HEAD", &ctx->METHOD_HEAD))
		ERROR_LOG_GOTO(ERR, "Cannot read constant for HEAD method");
	
	if(ERROR_CODE(int) == _read_const_u64("SEEK_SET", &ctx->RANGE_SEEK_SET))
		ERROR_LOG_GOTO(ERR, "Cannot read constant for file head");
	
	if(ERROR_CODE(int) == _read_const_u64("SEEK_END", &ctx->RANGE_SEEK_END))
		ERROR_LOG_GOTO(ERR, "Cannot read constant for file end");

	int rc = ERROR_CODE(int);

	rc = 0;
	goto RET;
ERR:
#ifdef LOG_ERROR_ENABLED
	LOG_ERROR("=========== libproto stack ============");
	const proto_err_t* stack = proto_err_stack();

	for(;stack != NULL; stack = stack->child)
	{
		char buf[1024];

		LOG_ERROR("%s", proto_err_str(stack, buf, sizeof(buf)));
	}
#endif

	proto_err_clear();
	LOG_ERROR("=========== end of libproto stack ============");

RET:
	if(ERROR_CODE(int) == proto_finalize())
		ERROR_RETURN_LOG(int, "Cannot finalize libproto");

	return rc;
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
