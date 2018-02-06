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
#include <options.h>

typedef struct {
	options_t               options;        /*!< The servlet options */

	pipe_t                  request;        /*!< The request data */
	pipe_t                  response;       /*!< The response data */
	pstd_type_accessor_t    url_acc;        /*!< The string token accessor for the URL */
	pstd_type_accessor_t    method_acc;     /*!< The method code accessor */
	pstd_type_accessor_t    data_acc;       /*!< The data accessor */
	pstd_type_accessor_t    priority_acc;   /*!< The priorty accessor */
	pstd_type_accessor_t    res_body_acc;   /*!< The response data accessor */
	pstd_type_accessor_t    res_header_acc; /*!< The response header or metadata */
	pstd_type_accessor_t    res_status_acc; /*!< The response status code */
	pstd_type_model_t*      type_model;     /*!< The type model for this servlet */

	struct {
		uint32_t            GET;          /*!< The GET method */
		uint32_t            POST;         /*!< The POST method */
		uint32_t            PUT;          /*!< The PUT method */
		uint32_t            HEAD;         /*!< The HEAD method */
	}                       methods;      /*!< The method codes */

} ctx_t;

/**
 * @brief The async buffer
 **/
typedef struct {
	int              posted;    /*!< If the task is posted */
	uint32_t         follow:1;  /*!< Follow redirect */
	const char*      data;      /*!< The data payload */
	client_request_t request;   /*!< The request data */
	enum {
		NONE,              /*!< For the request that is not a HTTP request */
		GET,               /*!< The GET HTTP method */
		POST,              /*!< The POST HTTP method */
		HEAD,              /*!< The HEAD HTTP method */
		PUT,               /*!< The PUT HTTP method */
	}           method;    /*!< The method code, only valid for HTTP and will be NONE for all other protocol */
} async_buf_t;


static int _init(uint32_t argc, char const* const* argv, void* data)
{
	ctx_t* ctx = (ctx_t*)data;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet initialization options");

	/**
	 * TODO: What if the data payload is extermely large ? We need to make the data section be a file token as well
	 **/
	ctx->request = pipe_define("request", PIPE_INPUT, "plumber/std_servlet/network/http/client/v0/Request");

	if(ctx->options.string)
		ctx->response = pipe_define("response", PIPE_OUTPUT, "plumber/std_servlet/network/http/client/v0/StringResponse");
	else 
		ctx->response = pipe_define("response", PIPE_OUTPUT, "plumber/std_servlet/network/http/client/v0/ObjectResponse");

	if(ERROR_CODE(int) == client_init(ctx->options.queue_size, ctx->options.num_parallel, ctx->options.num_threads))
	    ERROR_RETURN_LOG(int, "Cannot intialize the client library");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
	    ERROR_RETURN_LOG(int, "Cannot create the type model for this servlet");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->url_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "url.token")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.url.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->method_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "method")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.method");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->data_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "data.token")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.data.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->priority_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->request, "priority")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for request.priority");

	/* Since both string mode and object mode uses body.token for the RLS token, so there's no difference */
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->res_body_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->response, "body.token")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for response.body.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->res_header_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->response, "header.token")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for response.header.token");

	if(ctx->options.string && ERROR_CODE(pstd_type_accessor_t) == (ctx->res_status_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->response, "status")))
	    ERROR_RETURN_LOG(int, "Cannot get the field accessor for response.header.status");

	/* Load the constants */
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "GET", &ctx->methods.GET))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for method GET");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "POST", &ctx->methods.POST))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for method POST");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "PUT", &ctx->methods.PUT))
	    ERROR_RETURN_LOG(int, "Cannot get the constant for method POST");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->request, "HEAD", &ctx->methods.HEAD))
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

	if(buf->data != NULL && buf->data[0] != 0)
	{
		if(CURLE_OK != (curl_rc = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, buf->data)))
		    ERROR_RETURN_LOG(int, "Cannot set the POST data fields: %s", curl_easy_strerror(curl_rc));

	}

	if(buf->follow && CURLE_OK != (curl_rc = curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1)))
	    ERROR_RETURN_LOG(int, "Cannot set the follow redirection option: %s", curl_easy_strerror(curl_rc));

	switch(buf->method)
	{
		case POST:
		    curl_rc = curl_easy_setopt(handle, CURLOPT_HTTPPOST, 1);
		    break;
		case GET:
		    curl_rc = curl_easy_setopt(handle, CURLOPT_HTTPGET, 1);
		    break;
		case PUT:
		    curl_rc = curl_easy_setopt(handle, CURLOPT_PUT, 1);
		    break;
		case HEAD:
		    curl_rc = curl_easy_setopt(handle, CURLOPT_NOBODY, 1);
		    break;
		default:
		    curl_rc = CURLE_OK;
	}

	if(CURLE_OK != curl_rc)
	    ERROR_RETURN_LOG(int, "Cannot set HTTP request method: %s", curl_easy_strerror(curl_rc));

	return 0;
}

static int _before_request_started(void* data)
{
	async_handle_t* handle = (async_handle_t*)data;

	return async_cntl(handle, ASYNC_CNTL_SET_WAIT);
}

static int _async_setup(async_handle_t* handle, void* data, void* ctxbuf)
{
	ctx_t* ctx = (ctx_t*)ctxbuf;
	async_buf_t* abuf = (async_buf_t*)data;

	memset(abuf, 0, sizeof(*abuf));

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);
	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Cannot create new type instance from the type model");

	scope_token_t url_tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->url_acc);
	if(ERROR_CODE(scope_token_t) == url_tok)
	    ERROR_LOG_GOTO(ERR, "Cannot read URL token");

	const pstd_string_t* str = pstd_string_from_rls(url_tok);
	if(NULL == str || NULL == (abuf->request.uri = pstd_string_value(str)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the value of tstring for the URL");

	scope_token_t data_tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->data_acc);
	if(ERROR_CODE(scope_token_t) == data_tok)
	    ERROR_LOG_GOTO(ERR, "Cannot read data token");

	if(data_tok > 0)
	{
		str = pstd_string_from_rls(data_tok);
		if(NULL == str || NULL == (abuf->data = pstd_string_value(str)))
		    ERROR_LOG_GOTO(ERR, "Cannot get the value of the data string for the request");
	}
	else abuf->data = NULL;


	uint32_t method = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, inst, ctx->method_acc);
	abuf->method = NONE;

	/* Check if this request is a HTTP request */
	if(strncmp(abuf->request.uri, "http", 4) == 0 && (abuf->request.uri[4] == ':' || (abuf->request.uri[4] == 's' && abuf->request.uri[5] == ':')))
	{
		if(ctx->methods.GET == method)
		    abuf->method = GET;
		else if(ctx->methods.POST == method)
		    abuf->method = POST;
		else if(ctx->methods.PUT == method)
		    abuf->method = PUT;
		else if(ctx->methods.HEAD == method)
		    abuf->method = HEAD;
		else
		    ERROR_LOG_GOTO(ERR, "Invalid method code");
	}

	/* Finally we need to read the priority */
	if(ERROR_CODE(int32_t) == (abuf->request.priority = PSTD_TYPE_INST_READ_PRIMITIVE(int32_t, inst, ctx->priority_acc)))
	    ERROR_LOG_GOTO(ERR, "Cannot read the priority from the input");

	abuf->request.save_header = (ctx->options.save_header != 0);
	abuf->request.async_handle = handle;
	abuf->request.setup = _setup_request;
	abuf->request.setup_data = abuf;
	abuf->follow = (ctx->options.follow_redir != 0);

	/* We cannot set the servlet mode to the synchronized mode at this point. Since once we failed to 
	 * performe the nonblocking add, we need async_exec to run. But the wait mode will prevent the async_exec
	 * task from running. So the state will be set only when the task is about to start  */
	int rc = client_add_request(&abuf->request, 0, _before_request_started, handle);

	LOG_DEBUG("Client servlet started processing request %s async_handle = %p", abuf->request.uri, handle);

	if(rc == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "Cannot add request to the request queue");
	else if(rc == 0)
	    LOG_DEBUG("The queue is currently full, try to add request asynchronously");
	else
	{
		LOG_DEBUG("The request has been added to the queue successfully");
		abuf->posted = 1;
	}

	return 0;
ERR:
	pstd_type_instance_free(inst);
	return ERROR_CODE(int);
}

static inline int _write_string(pstd_type_instance_t* inst, pstd_type_accessor_t acc, char* str, size_t sz)
{
	if(NULL != str)
	{
		pstd_string_t* rls_obj = pstd_string_from_onwership_pointer(str, sz);

		if(rls_obj == NULL)
		{
			free(str);
			ERROR_RETURN_LOG(int, "Cannot create RLS object from the string");
		}

		scope_token_t tok = pstd_string_commit(rls_obj);

		if(ERROR_CODE(scope_token_t) == tok)
		{
			pstd_string_free(rls_obj);
			ERROR_RETURN_LOG(int, "Cannot dispose the RLS object to the scope");
		}

		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, acc, tok))
		    ERROR_RETURN_LOG(int, "Cannot write the token to pipe");
	}

	return 0;
}

static int _async_cleanup(async_handle_t* handle, void* data, void* ctxbuf)
{
	ctx_t* ctx = (ctx_t*)ctxbuf;
	async_buf_t* abuf = (async_buf_t*)data;
	int async_rc = 0;

	LOG_DEBUG("Client servlet finished processing request %s (handle = %p)", abuf->request.uri, handle);

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Cannot create type instance from the type model");

	if(ERROR_CODE(int) == async_cntl(handle, ASYNC_CNTL_RETCODE, &async_rc))
	    ERROR_LOG_GOTO(ERR, "Cannot access the return code of the async task");

	if(async_rc == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "The async task returns an error");

	if(abuf->request.curl_rc != CURLE_OK)
	    ERROR_LOG_GOTO(ERR, "Curl returns an error: %s (URI: %s)", curl_easy_strerror(abuf->request.curl_rc), abuf->request.uri);

	int rc = _write_string(inst, ctx->res_body_acc, abuf->request.result, abuf->request.result_sz);
	abuf->request.result = NULL;

	if(ERROR_CODE(int) == rc) ERROR_LOG_GOTO(ERR, "Cannot write result body to output");

	rc = _write_string(inst, ctx->res_header_acc, abuf->request.header, abuf->request.header_sz);
	abuf->request.header = NULL;

	if(ERROR_CODE(int) == rc) ERROR_LOG_GOTO(ERR, "Cannot write result header to output");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->res_status_acc, abuf->request.status_code))
	    ERROR_LOG_GOTO(ERR, "Cannot write status code to the output");

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
	    ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	return 0;
ERR:
	if(NULL != inst) pstd_type_instance_free(inst);
	if(NULL != abuf->request.result) free(abuf->request.result);
	if(NULL != abuf->request.header) free(abuf->request.header);
	return ERROR_CODE(int);
}

static int _async_exec(async_handle_t* handle, void* data)
{
	async_buf_t* abuf = (async_buf_t*)data;

	if(abuf->posted == 1) return 0;

	if(ERROR_CODE(int) == async_cntl(handle, ASYNC_CNTL_SET_WAIT))
	    ERROR_RETURN_LOG(int, "Cannot set the async task to wait mode");

	if(ERROR_CODE(int) == client_add_request(&abuf->request, 1, NULL, NULL))
	    ERROR_RETURN_LOG(int, "Cannot add the request to the client queue");
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
