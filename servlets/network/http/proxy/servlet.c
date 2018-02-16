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
	pipe_t    request;
	pipe_t    response;

	pstd_type_model_t* type_model;

	pstd_type_accessor_t url_token_acc;
	pstd_type_accessor_t data_token_acc;
	pstd_type_accessor_t method_acc;

	pstd_type_accessor_t res_token_acc;

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

	if(ERROR_CODE(pipe_t) == (ctx->request = pipe_define("request", PIPE_INPUT, "plumber/std_servlet/network/http/proxy/v0/Request")))
	    ERROR_RETURN_LOG(int, "Cannot define the request pipe");

	if(ERROR_CODE(pipe_t) == (ctx->response = pipe_define("response", PIPE_OUTPUT, "plumber/std_servlet/network/http/proxy/v0/Response")))
	    ERROR_RETURN_LOG(int, "Cannot define the response pipe");

	if(ERROR_CODE(int) == connection_pool_init(ctx->options.conn_pool_size, ctx->options.conn_per_peer))
	    ERROR_RETURN_LOG(int, "Cannot initialize the connection pool for this servlet instance");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
	    ERROR_RETURN_LOG(int, "Cannot create type model for the servlet");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->url_token_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "url.token")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.url.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->data_token_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "data.token")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.data.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->method_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "method")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.method");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->res_token_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->response, "token")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for response.token");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "GET", &ctx->method.GET))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for GET method");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "PUT", &ctx->method.PUT))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for PUT method");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "POST", &ctx->method.POST))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for POST method");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "HEAD", &ctx->method.HEAD))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for HEAD method");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "DELETE", &ctx->method.DELETE))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for DELETE method");

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

static inline int _read_string(pstd_type_instance_t* inst, pstd_type_accessor_t acc, const char** result, size_t* size)
{
	scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, acc);

	if(ERROR_CODE(scope_token_t) == token)
	    ERROR_RETURN_LOG(int, "Cannot read token from the pipe");

	if(token == 0) return 0;

	const pstd_string_t* str_obj = pstd_string_from_rls(token);

	if(NULL == str_obj)
	    ERROR_RETURN_LOG(int, "Cannot read the string object from the RLS");

	if(NULL == (*result = pstd_string_value(str_obj)))
	    ERROR_RETURN_LOG(int, "Cannot get the string value for the RLS object");

	if(NULL != size && ERROR_CODE(size_t) == (*size = pstd_string_length(str_obj)))
	    ERROR_RETURN_LOG(int, "Cannot get the string length of the RLS object");

	return 1;
}

static int _exec(void* ctxmem)
{
	int ret = 0;
	_ctx_t* ctx = (_ctx_t*)ctxmem;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Cannot create type instance from the type model");

	const char* url;
	int rc = _read_string(inst, ctx->url_token_acc, &url, NULL);
	if(rc == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "Cannot read the string");

	if(rc == 0) goto RET;

	const char* data = NULL;
	size_t data_size = 0;
	rc = _read_string(inst, ctx->data_token_acc, &data, &data_size);

	request_method_t method;

	uint32_t method_code = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, inst, ctx->method_acc);
	if(ERROR_CODE(uint32_t) == method_code)
	    ERROR_LOG_GOTO(ERR, "Cannot read the method code");

	if(method_code == ctx->method.GET)
	    method = REQUEST_METHOD_GET;
	else if(method_code == ctx->method.PUT)
	    method = REQUEST_METHOD_PUT;
	else if(method_code == ctx->method.POST)
	    method = REQUEST_METHOD_POST;
	else if(method_code == ctx->method.HEAD)
	    method = REQUEST_METHOD_HEAD;
	else if(method_code == ctx->method.DELETE)
	    method = REQUEST_METHOD_DELETE;
	else
	    goto RET;

	request_t* req = request_new(method, url, data, data_size, ctx->options.conn_timeout);

	if(NULL == req)
	    ERROR_LOG_GOTO(ERR,  "Cannot create the request");

	scope_token_t token = request_commit(req);

	if(ERROR_CODE(scope_token_t) == token)
	{
		request_free(req);
		ERROR_LOG_GOTO(ERR, "Cannot commit the token to the scope");
	}

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->res_token_acc, token))
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
