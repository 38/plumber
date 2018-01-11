/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>

#include <curl/curl.h>

#include <client.h>

typedef struct {
	uint32_t                num_threads;  /*!< The number of threads  this servlet requested for */
	uint32_t                num_parallel; /*!< The number parallel requests the client can do */
	uint32_t                queue_size;   /*!< The size of the request queue */
	pipe_t                  request;      /*!< The request data */
	pipe_t                  response;     /*!< The response data */
	pstd_type_accessor_t    url_acc;      /*!< The string token accessor for the URL */
	pstd_type_accessor_t    method_acc;   /*!< The method code accessor */
	pstd_type_accessor_t    data_acc;     /*!< The data accessor */
	pstd_type_accessor_t    response_acc; /*!< The response data accessor */
	pstd_type_accessor_t    priority_acc; /*!< The priorty accessor */
	pstd_type_model_t*      type_model;   /*!< The type model for this servlet */

	struct {
		uint32_t            GET;          /*!< The GET method */
		uint32_t            POST;         /*!< The POST method */
		/* TODO: Add other methods below */
	}                       methods;      /*!< The method codes */

} ctx_t;

/**
 * @brief The async buffer
 **/
typedef struct {
	int         posted;  /*!< If the task is posted */
	const char* url;     /*!< The URL we are requesting */
	const char* data;    /*!< The data payload */
	int32_t     priority;/*!< The priorty of this request */
	size_t      result_sz; /*!< The size of the result */
	char*       result;  /*!< The result buffer (TODO: what if the result is too large) */
	CURLcode    curl_rc; /*!< The CURL return code */
	enum {
		NONE,            /*!< For the request that is not a HTTP request */
		GET,             /*!< The GET HTTP method */
		POST             /*!< The POST HTTP method */
	}           method;  /*!< The method code, only valid for HTTP and will be NONE for all other protocol */
} async_buf_t;

static inline int _opt_callback(pstd_option_data_t data)
{
	switch(data.current_option->short_opt)
	{
		case 'T':
		case 'Q':
		case 'P':
			*(uint32_t*)((char*)data.current_option->args + (uintptr_t)data.cb_data) = (uint32_t)data.param_array[0].intval;
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid options");
	}

	return 0;
}


static int _init(uint32_t argc, char const* const* argv, void* data)
{
	ctx_t* ctx = (ctx_t*)data;

	ctx->num_threads  = 1;
	ctx->num_parallel = 128;
	ctx->queue_size   = 1024;
	
	static pstd_option_t opts[] = {
		{
			.long_opt    = "help",
			.short_opt   = 'h',
			.pattern     = "",
			.description = "Show this help message",
			.handler     = pstd_option_handler_print_help,
			.args        = NULL
		},
		{
			.long_opt    = "nthreads",
			.short_opt   = 'T',
			.pattern     = "I",
			.description = "Set the number of client threads can be used by the servlet [default value: 1]",
			.handler     = _opt_callback,
			.args        = &((ctx_t*)0)->num_threads
		},
		{
			.long_opt    = "parallel",
			.short_opt   = 'P',
			.pattern     = "I",
			.description = "Set the number of parallel request a thread can handle [default value: 128]",
			.handler     = _opt_callback,
			.args        = &((ctx_t*)0)->num_parallel
		},
		{
			.long_opt    = "queue-size",
			.short_opt   = 'Q',
			.pattern     = "I",
			.description = "Set the maximum size of the request queue [default value: 1024]",
			.handler     = _opt_callback,
			.args        = &((ctx_t*)0)->queue_size
		}
	};

	if(ERROR_CODE(int) == pstd_option_sort(opts, sizeof(opts) / sizeof(opts[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the options array");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(opts, sizeof(opts) / sizeof(opts[0]), argc, argv, ctx))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet initialization string");

	/**
	 * TODO: What if the data payload is extermely large ? We need to make the data section be a file token as well
	 **/
	ctx->request = pipe_define("request", PIPE_INPUT, "plumber/std_servlet/network/http/client/v0/Request");

	/**
	 * TODO: even though returning a string might be an option when the response is small. 
	 *       However for the larger response we finally needs to wrap the CURL object into a 
	 *       request local token. Thus we can read the result whever we need them
	 **/
	ctx->response = pipe_define("response", PIPE_OUTPUT, "plumber/std/request_local/String");

	if(ERROR_CODE(int) == client_init(ctx->queue_size, ctx->num_parallel, ctx->num_threads))
		ERROR_RETURN_LOG(int, "Cannot intialize the client library");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create the type model for this servlet");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->url_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "url.token")))
		ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.url.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->method_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "method")))
		ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.method");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->data_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "data.token")))
		ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.data.token");

	/* TODO: handle the larger response at this point */
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->response_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->response, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the field accessor for response.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->priority_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "priority")))
		ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.priority");

	/* Load the constants */
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "GET", &ctx->methods.GET))
		ERROR_RETURN_LOG(int, "Cannot get the constant for method GET");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "POST", &ctx->methods.POST))
		ERROR_RETURN_LOG(int, "Cannot get the constant for method POST");

	return 1;
}

static int _cleanup(void* data)
{
	ctx_t* ctx = (ctx_t*)data;
	if(ERROR_CODE(int) == client_finalize())
		ERROR_RETURN_LOG(int, "Cannot finalize the client library");

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		ERROR_RETURN_LOG(int, "Cannot dispose the type model");

	return 0;
}

static int _setup_request(CURL* handle, void* data)
{
	async_buf_t* buf = (async_buf_t*)data;
	CURLcode curl_rc;

	if(buf->method == POST && buf->data != NULL && buf->data[0] != 0)
	{
		if(CURLE_OK != (curl_rc = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, buf->data)))
			ERROR_RETURN_LOG(int, "Cannot set the POST data fields: %s", curl_easy_strerror(curl_rc));

	}

	switch(buf->method)
	{
		case POST:
			if(CURLE_OK != (curl_rc = curl_easy_setopt(handle, CURLOPT_HTTPPOST, 1)))
				ERROR_RETURN_LOG(int, "Cannot set the HTTP request method to POST: %s", curl_easy_strerror(curl_rc));
			break;
		case GET:
			if(CURLE_OK != (curl_rc = curl_easy_setopt(handle, CURLOPT_HTTPGET, 1)))
				ERROR_RETURN_LOG(int, "Cannot set the HTTP request method to GET: %s", curl_easy_strerror(curl_rc));
			break;
			/* For default case, we simply do nothing */
		default:
			(void)0;
	}

	return 0;
}

static int _async_setup(async_handle_t* handle, void* data, void* ctxbuf)
{
	ctx_t* ctx = (ctx_t*)ctxbuf;
	async_buf_t* abuf = (async_buf_t*)data;

	abuf->posted = 0;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);
	if(NULL == inst) 
		ERROR_RETURN_LOG(int, "Cannot create new type instance from the type model");

	scope_token_t url_tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->url_acc);
	if(ERROR_CODE(scope_token_t) == url_tok) 
		ERROR_LOG_GOTO(ERR, "Cannot read URL token");

	const pstd_string_t* str = pstd_string_from_rls(url_tok);
	if(NULL == str || NULL == (abuf->url = pstd_string_value(str)))
		ERROR_LOG_GOTO(ERR, "Cannot get the value of tstring for the URL");

	scope_token_t data_tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->data_acc);
	if(ERROR_CODE(scope_token_t) == data_tok)
		ERROR_LOG_GOTO(ERR, "Cannot read data token");

	str = pstd_string_from_rls(data_tok);
	if(NULL == str || NULL == (abuf->data = pstd_string_value(str)))
		ERROR_LOG_GOTO(ERR, "Cannot get the value of the data string for the request");


	uint32_t method = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, inst, ctx->method_acc);
	abuf->method = NONE;

	/* Check if this request is a HTTP request */
	if(strncmp(abuf->url, "http", 4) == 0 && (abuf->url[4] == ':' || (abuf->url[4] == 's' && abuf->url[5] == ':')))
	{
		if(ctx->methods.GET == method)
			abuf->method = GET;
		else if(ctx->methods.POST == method)
			abuf->method = POST;
		else
			ERROR_LOG_GOTO(ERR, "Invalid method code");
	}

	/* Finally we need to read the priority */
	if(ERROR_CODE(int32_t) == (abuf->priority = PSTD_TYPE_INST_READ_PRIMITIVE(int32_t, inst, ctx->priority_acc)))
		ERROR_LOG_GOTO(ERR, "Cannot read the priority from the input");

	abuf->result = NULL;

	int rc = client_add_request(abuf->url, handle, abuf->priority, 
			                    0, _setup_request, abuf, &abuf->result, 
								&abuf->result_sz, &abuf->curl_rc);

	if(rc == ERROR_CODE(int))
		ERROR_LOG_GOTO(ERR, "Cannot add request to the request queue");
	else if(rc == 0) 
		LOG_DEBUG("The queue is currently full, try to add request asynchronously");
	else
	{
		LOG_DEBUG("The request has been added to the queue successfully");
		abuf->posted = 1;
		if(ERROR_CODE(int) == async_cntl(handle, ASYNC_CNTL_SET_WAIT))
			ERROR_LOG_GOTO(ERR, "Cannot make the async task into wait mode");
	}

	return 0;
ERR:
	pstd_type_instance_free(inst);
	return ERROR_CODE(int);
}

static int _async_cleanup(async_handle_t* handle, void* data, void* ctxbuf)
{
	ctx_t* ctx = (ctx_t*)ctxbuf;
	async_buf_t* abuf = (async_buf_t*)data;
	int async_rc = 0;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == inst) 
		ERROR_RETURN_LOG(int, "Cannot create type instance from the type model");

	if(ERROR_CODE(int) == async_cntl(handle, ASYNC_CNTL_RETCODE, &async_rc))
		ERROR_LOG_GOTO(ERR, "Cannot access the return code of the async task");

	if(async_rc == ERROR_CODE(int))
		ERROR_LOG_GOTO(ERR, "The async task returns an error");


	/* Then we need to write the result back to the pipe */
	if(NULL != abuf->result)
	{
		LOG_DEBUG("The async request result a nonempty result, adding to RLS");

		pstd_string_t* str = pstd_string_from_onwership_pointer(abuf->result, abuf->result_sz);

		if(NULL == str)
			ERROR_LOG_GOTO(ERR, "Cannot create new RLS string from the request result");

		scope_token_t tok;

		if(ERROR_CODE(scope_token_t) == (tok = pstd_string_commit(str)))
		{
			pstd_string_free(str);
			ERROR_LOG_GOTO(ERR, "Cannot commit the result string to the RLS");
		}

		abuf->result = NULL;

		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->response_acc, tok))
			ERROR_LOG_GOTO(ERR, "Cannot write the result to the output pipe");
	}

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	return 0;
ERR:
	if(NULL != inst) pstd_type_instance_free(inst);
	if(NULL != abuf->result) free(abuf->result);
	return ERROR_CODE(int);
}

static int _async_exec(async_handle_t* handle, void* data)
{
	async_buf_t* abuf = (async_buf_t*)data;

	if(abuf->posted == 1) return 0;

	if(client_add_request(abuf->url, handle, abuf->priority, 1, 
				          _setup_request, abuf, &abuf->result, 
						  &abuf->result_sz, &abuf->curl_rc) == ERROR_CODE(int))
		ERROR_RETURN_LOG(int, "Cannot add the request to the request queue");

	if(ERROR_CODE(int) == async_cntl(handle, ASYNC_CNTL_SET_WAIT))
		ERROR_RETURN_LOG(int, "Cannot make the async task into wait mode");

	return 0;
}

SERVLET_DEF = {
	.size = sizeof(ctx_t),
	.async_buf_size = sizeof(async_buf_t),
	.desc = "The HTTP client servlet",
	.version = 0x0,
	.init = _init,
	.unload = _cleanup,
	.async_setup = _async_setup,
	.async_cleanup = _async_cleanup,
	.async_exec = _async_exec
};
