/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>

#include <options.h>
#include <connection.h>
#include <request.h>

typedef struct {
	options_t options;

	pipe_t    p_request;
	pipe_t    p_response;

	pstd_type_model_t* type_model;

	pstd_type_accessor_t    a_method;
	pstd_type_accessor_t    a_host;
	pstd_type_accessor_t    a_base_url;
	pstd_type_accessor_t    a_rel_url;
	pstd_type_accessor_t    a_query_param;
	pstd_type_accessor_t    a_body;
	pstd_type_accessor_t    a_range_begin;
	pstd_type_accessor_t    a_range_end;
	pstd_type_accessor_t    a_response;

	struct {
		uint32_t GET;
		uint32_t PUT;
		uint32_t POST;
		uint32_t HEAD;
		uint32_t DELETE;
	}         method;
} _ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	_ctx_t* ctx = (_ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
	    ERROR_RETURN_LOG(int, "Invalid servlete initialization string");

	if(ERROR_CODE(pipe_t) == (ctx->p_request = pipe_define("request", PIPE_INPUT, "plumber/std_servlet/network/http/parser/v0/RequestData")))
	    ERROR_RETURN_LOG(int, "Cannot define the request pipe");

	if(ERROR_CODE(pipe_t) == (ctx->p_response = pipe_define("response", PIPE_OUTPUT, "plumber/std_servlet/network/http/proxy/v0/Response")))
	    ERROR_RETURN_LOG(int, "Cannot define the response pipe");

	if(ERROR_CODE(int) == connection_pool_init(ctx->options.conn_pool_size, ctx->options.conn_per_peer))
	    ERROR_RETURN_LOG(int, "Cannot initialize the connection pool for this servlet instance");

	PSTD_TYPE_MODEL(type_list)
	{
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, method,             ctx->a_method),
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, host.token,         ctx->a_host),
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, base_url.token,     ctx->a_base_url),
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, relative_url.token, ctx->a_rel_url),
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, query_param.token,  ctx->a_query_param),
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, body.token,         ctx->a_body),
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, range_begin,        ctx->a_range_begin),
		PSTD_TYPE_MODEL_FIELD(ctx->p_request, range_end,          ctx->a_range_end),
		PSTD_TYPE_MODEL_CONST(ctx->p_request, METHOD_GET,         ctx->method.GET),
		PSTD_TYPE_MODEL_CONST(ctx->p_request, METHOD_PUT,         ctx->method.PUT),
		PSTD_TYPE_MODEL_CONST(ctx->p_request, METHOD_POST,        ctx->method.POST),
		PSTD_TYPE_MODEL_CONST(ctx->p_request, METHOD_HEAD,        ctx->method.HEAD),
		PSTD_TYPE_MODEL_CONST(ctx->p_request, METHOD_DELETE,      ctx->method.DELETE),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response, token,             ctx->a_response)
	};

	if(NULL == (ctx->type_model = PSTD_TYPE_MODEL_BATCH_INIT(type_list)))
		ERROR_RETURN_LOG(int, "Cannot initialize the type model");

	return 0;
}

static int _cleanup(void* ctxmem)
{
	int ret = 0;

	_ctx_t* ctx = (_ctx_t*)ctxmem;

	if(ERROR_CODE(int) == connection_pool_finalize())
	{
		ret = ERROR_CODE(int);
		LOG_ERROR("Cannot finalize the connection pool");
	}


	if(ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
	{
		ret = ERROR_CODE(int);
		LOG_ERROR("Cannot finalize the type model");
	}

	return ret;
}

static inline int _construct_request_param(const _ctx_t* ctx, pstd_type_instance_t* inst, request_param_t* buf)
{
	uint32_t method_code = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, inst, ctx->a_method);
	if(ERROR_CODE(uint32_t) == method_code)
		ERROR_RETURN_LOG(int, "Cannot read the method code");

	if(method_code == ctx->method.GET)
	    buf->method = REQUEST_METHOD_GET;
	else if(method_code == ctx->method.PUT)
	    buf->method = REQUEST_METHOD_PUT;
	else if(method_code == ctx->method.POST)
	    buf->method = REQUEST_METHOD_POST;
	else if(method_code == ctx->method.HEAD)
	    buf->method = REQUEST_METHOD_HEAD;
	else if(method_code == ctx->method.DELETE)
	    buf->method = REQUEST_METHOD_DELETE;
	else 
		return 0;

	if(NULL == (buf->host = pstd_string_get_data_from_accessor(inst, ctx->a_host, NULL)))
		ERROR_RETURN_LOG(int, "Cannot read the host from the request");
	
	if(NULL == (buf->base_dir = pstd_string_get_data_from_accessor(inst, ctx->a_base_url, "")))
		ERROR_RETURN_LOG(int, "Cannot read the base URL from the request");
	
	if(NULL == (buf->relative_path = pstd_string_get_data_from_accessor(inst, ctx->a_rel_url, "")))
		ERROR_RETURN_LOG(int, "Cannot read the relative path from the request");

	if(NULL == (buf->query_param = pstd_string_get_data_from_accessor(inst, ctx->a_query_param, "")))
		ERROR_RETURN_LOG(int, "Cannot read the query param from the request");

	const pstd_string_t* content_obj = pstd_string_from_accessor(inst, ctx->a_body);

	if(NULL != content_obj)
	{
		if(NULL == (buf->content = pstd_string_value(content_obj)))
			ERROR_RETURN_LOG(int, "Cannot get the value of the content object");

		if(ERROR_CODE(size_t) == (buf->content_len = pstd_string_length(content_obj)))
			ERROR_RETURN_LOG(int, "Cannot get the length of the string");
	}
	else
	{
		buf->content = NULL;
		buf->content_len = 0;
	}

	buf->range_begin = PSTD_TYPE_INST_READ_PRIMITIVE(uint64_t, inst, ctx->a_range_begin);
	buf->range_end   = PSTD_TYPE_INST_READ_PRIMITIVE(uint64_t, inst, ctx->a_range_end);

	if(buf->range_begin == 0) buf->range_begin = (uint64_t)-1;

	return 1;
}

static int _exec(void* ctxmem)
{
	int ret = 0;
	_ctx_t* ctx = (_ctx_t*)ctxmem;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Cannot create type instance from the type model");

	request_param_t rp;
	switch(_construct_request_param(ctx, inst, &rp))
	{
		case 0:                   goto RET;
		case ERROR_CODE(int):     goto ERR;
	}


	request_t* req = request_new(&rp, ctx->options.conn_timeout);

	if(NULL == req)
	    ERROR_LOG_GOTO(ERR,  "Cannot create the request");

	scope_token_t token = request_commit(req);

	if(ERROR_CODE(scope_token_t) == token)
	{
		request_free(req);
		ERROR_LOG_GOTO(ERR, "Cannot commit the token to the scope");
	}

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->a_response, token))
	    ERROR_LOG_GOTO(ERR, "Cannot write the token to the scope");

	goto RET;

ERR:
	ret = ERROR_CODE(int);
RET:
	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
	    ERROR_RETURN_LOG(int, "Cannot dispose the instance");

	return ret;
}

SERVLET_DEF = {
	.desc    = "The HTTP Proxy Servlet",
	.version = 0x0,
	.size    = sizeof(_ctx_t),
	.init    = _init,
	.unload  = _cleanup,
	.exec    = _exec
};
