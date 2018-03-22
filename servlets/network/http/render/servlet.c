/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <pstd/types/string.h>
#include <pstd/types/file.h>

#include <options.h>
#include <zlib_token.h>
#include <chunked.h>

enum {
	_ENCODING_GZIP    = 1,
	_ENCODING_DEFLATE = 2,
	_ENCODING_BR      = 4,
	_ENCODING_CHUNKED = 8,
	_ENCODING_COMPRESSED = 7
};

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t            opts;               /*!< The servlet options */
	pipe_t               p_response;         /*!< The pipe for the response to render */
	pipe_t               p_proxy;            /*!< The reverse proxy response */
	pipe_t               p_protocol_data;    /*!< The protocol data */
	pipe_t               p_500;              /*!< The 500 status signal pipe */
	pipe_t               p_output;           /*!< The output port */

	pstd_type_model_t*   type_model;         /*!< The type model for this servlet */
	pstd_type_accessor_t a_status_code;      /*!< The accessor for the status_code */
	pstd_type_accessor_t a_body_flags;       /*!< The accessor for body flags */
	pstd_type_accessor_t a_body_size;        /*!< The accessor for the body size */
	pstd_type_accessor_t a_body_token;       /*!< The accessor for RLS token */
	pstd_type_accessor_t a_mime_type;        /*!< The MIME type RLS token */
	pstd_type_accessor_t a_redir_loc;        /*!< The redirect location RLS token accessor */
	pstd_type_accessor_t a_proxy_token;      /*!< The reverse proxy token */
	pstd_type_accessor_t a_range_begin;      /*!< The accessor for the begin offset of the range */
	pstd_type_accessor_t a_range_end;        /*!< The accessor for the end offset of the range */
	pstd_type_accessor_t a_range_total;      /*!< The accessor for the total size of the ranged body */

	pstd_type_accessor_t a_accept_enc;       /*!< The accept encoding RLS token */
	pstd_type_accessor_t a_upgrade_target;   /*!< The target we where we want to upgrade the protocol */
	pstd_type_accessor_t a_protocol_error;   /*!< The protocol error code */

	uint32_t             BODY_SIZE_UNKNOWN;  /*!< The unknown body size */
	uint32_t             BODY_CAN_COMPRESS;  /*!< Indicates if the body should be compressed */
	uint32_t             BODY_SEEKABLE;      /*!< Indicates if we can seek the body */
	uint32_t             BODY_RANGED;        /*!< Indicates if we got a ranged body */

	uint32_t             PROTOCOL_ERROR_BAD_REQ;  /*!< Indicate we have got a bad request */
} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->opts))
	    ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	PIPE_LIST(pipes)
	{
		PIPE("response",        PIPE_INPUT,               "plumber/std_servlet/network/http/render/v0/Response",     ctx->p_response),
		PIPE("protocol_data",   PIPE_INPUT,               "plumber/std_servlet/network/http/parser/v0/ProtocolData", ctx->p_protocol_data),
		PIPE("500",             PIPE_INPUT,               NULL,                                                      ctx->p_500),
		PIPE("output",          PIPE_OUTPUT | PIPE_ASYNC, NULL,                                                      ctx->p_output)
	};

	if(ERROR_CODE(int) == PIPE_BATCH_INIT(pipes)) return ERROR_CODE(int);

	PSTD_TYPE_MODEL(model_list)
	{
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        status.status_code,       ctx->a_status_code),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        body_object,              ctx->a_body_token),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        body_flags,               ctx->a_body_flags),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        body_size,                ctx->a_body_size),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        mime_type.token,          ctx->a_mime_type),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        redirect_location.token,  ctx->a_redir_loc),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        range_begin,              ctx->a_range_begin),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        range_end,                ctx->a_range_end),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        range_total,              ctx->a_range_total),
		PSTD_TYPE_MODEL_FIELD(ctx->p_protocol_data,   accept_encoding.token,    ctx->a_accept_enc),
		PSTD_TYPE_MODEL_FIELD(ctx->p_protocol_data,   upgrade_target.token,     ctx->a_upgrade_target),
		PSTD_TYPE_MODEL_FIELD(ctx->p_protocol_data,   error,                    ctx->a_protocol_error),
		PSTD_TYPE_MODEL_CONST(ctx->p_response,        BODY_SIZE_UNKNOWN,        ctx->BODY_SIZE_UNKNOWN),
		PSTD_TYPE_MODEL_CONST(ctx->p_response,        BODY_CAN_COMPRESS,        ctx->BODY_CAN_COMPRESS),
		PSTD_TYPE_MODEL_CONST(ctx->p_response,        BODY_SEEKABLE,            ctx->BODY_SEEKABLE),
		PSTD_TYPE_MODEL_CONST(ctx->p_response,        BODY_RANGED,              ctx->BODY_RANGED),
		PSTD_TYPE_MODEL_CONST(ctx->p_protocol_data,   ERROR_BAD_REQ,            ctx->PROTOCOL_ERROR_BAD_REQ)
	};

	if(NULL == (ctx->type_model = PSTD_TYPE_MODEL_BATCH_INIT(model_list))) return ERROR_CODE(int);

	if(ctx->opts.reverse_proxy)
	{
		if(ERROR_CODE(pipe_t) == (ctx->p_proxy = pipe_define("proxy", PIPE_INPUT, "plumber/std_servlet/network/http/proxy/v0/Response")))
		    ERROR_RETURN_LOG(int, "Cannot declare the proxy pipe");

		if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_proxy_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_proxy, "token")))
		    ERROR_RETURN_LOG(int, "Cannot get the accessor for proxy.token");
	}

	return 0;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;

	if(ERROR_CODE(int) == options_free(&ctx->opts))
	    rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
	    rc = ERROR_CODE(int);

	return rc;
}

/**
 * @brief Write the status line
 **/
static inline int _write_status_line(pstd_bio_t* bio, uint16_t status_code)
{
	const char* status_phrase = "Unknown Status Phrase";
	size_t status_size = 0;

#define STATUS_LINE(code, text) "HTTP/1.1 "#code" "text"\r\n"
#define STATUS_PHRASE(code, text) case code: status_phrase = STATUS_LINE(code, text); status_size = sizeof(STATUS_LINE(code, text)) - 1; break

	switch(status_code)
	{
		STATUS_PHRASE(100,"Continue");
		STATUS_PHRASE(101,"Switching Protocols");
		STATUS_PHRASE(102,"Processing");
		STATUS_PHRASE(103,"Early Hints");
		STATUS_PHRASE(200,"OK");
		STATUS_PHRASE(201,"Created");
		STATUS_PHRASE(202,"Accepted");
		STATUS_PHRASE(203,"Non-Authoritative Information");
		STATUS_PHRASE(204,"No Content");
		STATUS_PHRASE(205,"Reset Content");
		STATUS_PHRASE(206,"Partial Content");
		STATUS_PHRASE(207,"Multi-Status");
		STATUS_PHRASE(208,"Already Reported");
		STATUS_PHRASE(226,"IM Used");
		STATUS_PHRASE(300,"Multiple Choices");
		STATUS_PHRASE(301,"Moved Permanently");
		STATUS_PHRASE(302,"Found");
		STATUS_PHRASE(303,"See Other");
		STATUS_PHRASE(304,"Not Modified");
		STATUS_PHRASE(305,"Use Proxy");
		STATUS_PHRASE(306,"Switch Proxy");
		STATUS_PHRASE(307,"Temporary Redirect");
		STATUS_PHRASE(308,"Permanent Redirect");
		STATUS_PHRASE(400,"Bad Request");
		STATUS_PHRASE(401,"Unauthorized");
		STATUS_PHRASE(402,"Payment Required");
		STATUS_PHRASE(403,"Forbidden");
		STATUS_PHRASE(404,"Not Found");
		STATUS_PHRASE(405,"Method Not Allowed");
		STATUS_PHRASE(406,"Not Acceptable");
		STATUS_PHRASE(407,"Proxy Authentication Required");
		STATUS_PHRASE(408,"Request Timeout");
		STATUS_PHRASE(409,"Conflict");
		STATUS_PHRASE(410,"Gone");
		STATUS_PHRASE(411,"Length Required");
		STATUS_PHRASE(412,"Precondition Failed");
		STATUS_PHRASE(413,"Payload Too Large");
		STATUS_PHRASE(414,"URI Too Long");
		STATUS_PHRASE(415,"Unsupported Media Type");
		STATUS_PHRASE(416,"Range Not Satisfiable");
		STATUS_PHRASE(417,"Expectation Failed");
		STATUS_PHRASE(418,"I'm a teapot");
		STATUS_PHRASE(421,"Misdirected Request");
		STATUS_PHRASE(422,"Unprocessable Entity");
		STATUS_PHRASE(423,"Locked");
		STATUS_PHRASE(424,"Failed Dependency");
		STATUS_PHRASE(426,"Upgrade Required");
		STATUS_PHRASE(428,"Precondition Required");
		STATUS_PHRASE(429,"Too Many Requests");
		STATUS_PHRASE(431,"Request Header Fields Too Large");
		STATUS_PHRASE(451,"Unavailable For Legal Reasons");
		STATUS_PHRASE(500,"Internal Server Error");
		STATUS_PHRASE(501,"Not Implemented");
		STATUS_PHRASE(502,"Bad Gateway");
		STATUS_PHRASE(503,"Service Unavailable");
		STATUS_PHRASE(504,"Gateway Timeout");
		STATUS_PHRASE(505,"HTTP Version Not Supported");
		STATUS_PHRASE(506,"Variant Also Negotiates");
		STATUS_PHRASE(507,"Insufficient Storage");
		STATUS_PHRASE(508,"Loop Detected");
		STATUS_PHRASE(510,"Not Extended");
		STATUS_PHRASE(511,"Network Authentication Required");
		default:
		    ERROR_RETURN_LOG(int, "Invalid status code %d", status_code);
	}

	while(status_size > 0)
	{
		size_t rc = pstd_bio_write(bio, status_phrase, status_size);
		if(rc == ERROR_CODE(size_t))
		    ERROR_RETURN_LOG(int, "Cannot write the status line");
		status_size -= rc;
		status_phrase += rc;
	}

	return 0;
}

/**
 * @brief Write a string HTTP header field
 **/
static inline int _write_string_field(pstd_bio_t* bio, pstd_type_instance_t* inst, pstd_type_accessor_t acc, const char* name, const char* defval)
{
	const char* value = defval;

	if(NULL == (value = pstd_string_get_data_from_accessor(inst, acc, defval)))
	    ERROR_RETURN_LOG(int, "Cannot get the string value");

	if(ERROR_CODE(size_t) == pstd_bio_puts(bio, name))
	    ERROR_RETURN_LOG(int, "Cannot write the field name %s", name);

	if(ERROR_CODE(size_t) == pstd_bio_puts(bio, value))
	    ERROR_RETURN_LOG(int, "Cannot write the field value");

	if(ERROR_CODE(size_t) == pstd_bio_puts(bio, "\r\n"))
	    ERROR_RETURN_LOG(int, "Cannot write the CLRF");

	return 0;
}

/**
 * @brief Determine the best compression algorithm for this request
 **/
static inline uint32_t _determine_compression_algorithm(const ctx_t *ctx, pstd_type_instance_t* inst, int compress_enabled)
{
	if(pipe_eof(ctx->p_protocol_data) == 1)
	    return 0;

	const char* accepts = pstd_string_get_data_from_accessor(inst, ctx->a_accept_enc, "");
	if(NULL == accepts) ERROR_RETURN_LOG(uint32_t, "Cannot get the Accept-Encoding field");
	const char* accepts_end = accepts + strlen(accepts);

	unsigned current_len = 0;
	const char* ptr;
	uint32_t ret = 0;
	uint32_t compressed = !compress_enabled || !(ctx->opts.gzip_enabled || ctx->opts.deflate_enabled || ctx->opts.br_enabled);
	for(ptr = accepts; ptr < accepts_end && !compressed; ptr ++)
	{
		if(current_len == 0)
		{
			if(*ptr == ' ' || *ptr == '\t')
			    continue;
			else
			{
				switch(compressed ? -1 : *ptr)
				{
#ifdef HAS_ZLIB
					case 'g':
					    /* gzip */
					    if(ctx->opts.gzip_enabled && accepts_end - ptr >= 4 && memcmp("gzip", ptr, 4) == 0)
					         ret |= _ENCODING_GZIP, compressed = 1;
					    break;
					case 'd':
					    /* deflate */
					    if(ctx->opts.deflate_enabled && accepts_end - ptr >= 7 && memcmp("deflate", ptr, 7) == 0)
					        ret |= _ENCODING_DEFLATE, compressed = 1;
					    break;
#endif
#ifdef HAS_BROTLI
					case 'b':
					    /* br */
					    if(ctx->opts.br_enabled && accepts_end - ptr >= 2 && memcmp("br", ptr, 2) == 0)
					        ret |= _ENCODING_BR, compressed = 1;
					    break;
#endif
					default:
					    if(NULL == (ptr = strchr(ptr, ',')))
					        return ret;
				}
			}
		}
	}

	if(ret > 0) ret |= _ENCODING_CHUNKED;
	return ret;
}

/**
 * @brief Write encoding HTTP header
 **/
static inline int _write_encoding(pstd_bio_t* bio, uint32_t algorithm, uint64_t size)
{
	if((algorithm & _ENCODING_COMPRESSED))
	{
#define _CE_NAME "Content-Encoding: "
		const char* algorithm_name = _CE_NAME"identity\r\n";
		if((algorithm & _ENCODING_GZIP))
		    algorithm_name = _CE_NAME"gzip\r\n";
		else if((algorithm & _ENCODING_DEFLATE))
		    algorithm_name = _CE_NAME"deflate\r\n";
#ifdef HAS_BROTLI
		else if((algorithm & _ENCODING_BR))
		    algorithm_name = _CE_NAME"br\r\n";
#endif
		if(ERROR_CODE(size_t) == pstd_bio_puts(bio, algorithm_name))
		    ERROR_RETURN_LOG(int, "Cannot write the content-encoding");
	}

	if((algorithm & _ENCODING_CHUNKED))
	{
		if(ERROR_CODE(size_t) == pstd_bio_puts(bio, "Transfer-Encoding: chunked\r\n"))
		    ERROR_RETURN_LOG(int, "Cannot write the Transfer-Encoding header");
	}
	else
	{
		char buffer[256];
		char* ptr = buffer + sizeof(buffer) - 3;
		buffer[sizeof(buffer) - 1] = 0;
		buffer[sizeof(buffer) - 2] = '\n';
		buffer[sizeof(buffer) - 3] = '\r';

		if(size == 0)
		    *(--ptr) = '0';
		else while(size > 0)
		{
			*(--ptr) = (char)((size % 10) + '0');
			size /= 10;
		}

#define _CL_NAME "Content-Length: "

		ptr -= sizeof(_CL_NAME) - 1;
		memcpy(ptr, _CL_NAME, sizeof(_CL_NAME) - 1);

		while(ptr[0] != 0)
		{
			size_t sz;
			if(ERROR_CODE(size_t) == (sz = pstd_bio_write(bio, ptr, (size_t)(buffer + sizeof(buffer) - 1 - ptr))))
			    ERROR_RETURN_LOG(int, "Cannot write the Content-Length header");

			ptr += sz;
		}
	}

	return 0;
}

/**
 * @brief Write an error response
 **/
static inline scope_token_t _write_error_page(pstd_bio_t* bio, uint16_t status, const options_error_page_t* page, const char* default_page)
{
	if(ERROR_CODE(int) == _write_status_line(bio, status))
	    ERROR_RETURN_LOG(scope_token_t, "Cannot write the status line");

	size_t length;
	scope_token_t ret = ERROR_CODE(scope_token_t);

	if(NULL == page->error_page)
	{
DEF_ERR_PAGE:
		length = strlen(default_page);
		if(ERROR_CODE(scope_token_t) == (ret = pstd_string_create_commit(default_page)))
		    ERROR_RETURN_LOG(scope_token_t, "Cannot commit the default page to the RLS");
	}
	else
	{
		pstd_file_t* err_page = pstd_file_new(page->error_page);
		if(NULL == err_page)
		    ERROR_RETURN_LOG(scope_token_t, "Cannot create RLS file object for the error page");

		int exist = pstd_file_exist(err_page);
		if(exist == ERROR_CODE(int))
		    ERROR_LOG_GOTO(ERR, "Cannot check if the error page exists");

		if(!exist) goto DEF_ERR_PAGE;

		if(ERROR_CODE(size_t) == (length = pstd_file_size(err_page)))
		    ERROR_LOG_GOTO(ERR, "Cannot get the size of the error page");

		if(ERROR_CODE(scope_token_t) == (ret = pstd_file_commit(err_page)))
		    ERROR_LOG_GOTO(ERR, "Cannot commit the RLS file object to scope");

		goto WRITE;
ERR:
		pstd_file_free(err_page);
		return ERROR_CODE(scope_token_t);
	}

WRITE:

	if(ERROR_CODE(size_t) == pstd_bio_printf(bio, "Content-Type: %s\r\n"
	                                           "Content-Length: %zu\r\n", page->mime_type, (size_t)length))
	    ERROR_RETURN_LOG(scope_token_t, "Cannot write the header");

	return ret;
}

/**
 * @brief Write the connection control field
 **/
static inline int _write_connection_field(pstd_bio_t* out, pipe_t res, int needs_close)
{
	pipe_flags_t flags = 0;

	if(needs_close == 0)
	{
		if(ERROR_CODE(int) == pipe_cntl(res, PIPE_CNTL_GET_FLAGS, &flags))
		    ERROR_RETURN_LOG(int, "Cannot get the pipe flags");
	}
	else
	{
		if(ERROR_CODE(int) == pipe_cntl(res, PIPE_CNTL_CLR_FLAG, PIPE_PERSIST))
		    ERROR_RETURN_LOG(int, "Cannot clear the persistent flag");
	}

	if((flags & PIPE_PERSIST))
	{
		if(ERROR_CODE(size_t) == pstd_bio_puts(out, "Connection: keep-alive\r\n"))
		    ERROR_RETURN_LOG(int, "Cannot write the connection field");
	}
	else
	{
		if(ERROR_CODE(size_t) == pstd_bio_puts(out, "Connection: close\r\n"))
		    ERROR_RETURN_LOG(int, "Cannot write the connection field");
	}

	return 0;
}

static int _exec(void* ctxmem)
{
	uint16_t status_code;
	uint32_t body_flags, algorithm, protocol_error;
	uint64_t body_size = ERROR_CODE(uint64_t);
	scope_token_t body_token;
	int eof_rc;

	pstd_bio_t* out = NULL;
	ctx_t* ctx = (ctx_t*)ctxmem;
	pstd_type_instance_t* type_inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == type_inst)
	    ERROR_RETURN_LOG(int, "Cannot create type instance for the servlet");

	if(NULL == (out = pstd_bio_new(ctx->p_output)))
	    ERROR_LOG_GOTO(ERR, "Cannot create new pstd BIO object for the output pipe");

	/* Check if we need a HTTP 500 */
	if(ERROR_CODE(int) == (eof_rc = pipe_eof(ctx->p_500)))
	    ERROR_LOG_GOTO(ERR, "Cannot check if we got service internal error signal");

	if(!eof_rc)
	{
		const char* default_500 = "<html><body><center><h1>500 Server Internal Error</h1></center><hr/></body></html>";
		if(ERROR_CODE(scope_token_t) == (body_token = _write_error_page(out, 500, &ctx->opts.err_500, default_500)))
		    ERROR_LOG_GOTO(ERR, "Cannot write the HTTP 500 response");

		if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 1))
		    ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

		goto RET;
	}

	if(ERROR_CODE(int) == (eof_rc = pipe_eof(ctx->p_protocol_data)))
	    ERROR_LOG_GOTO(ERR, "Cannot check if we got the protocol data");

	if(!eof_rc)
	{
		if(ERROR_CODE(uint32_t) == (protocol_error = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, type_inst, ctx->a_protocol_error)))
		    ERROR_LOG_GOTO(ERR, "Cannot read the protocol error");

		if(protocol_error == ctx->PROTOCOL_ERROR_BAD_REQ)
		{
			const char* default_400 = "<html><body><center><h1>400 Bad Request</h1></center><hr/></body></html>";

			if(ERROR_CODE(scope_token_t) == (body_token = _write_error_page(out, 400, &ctx->opts.err_400, default_400)))
			    ERROR_LOG_GOTO(ERR, "Cannot write the HTTP 500 response");

			if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 1))
			    ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

			goto RET;
		}

		const char* target = pstd_string_get_data_from_accessor(type_inst, ctx->a_upgrade_target, "");
		if(target[0] != 0)
		{
			if(ERROR_CODE(int) == _write_status_line(out, 301))
			    ERROR_LOG_GOTO(ERR, "Cannot write the status line");

			if(ERROR_CODE(size_t) == pstd_bio_puts(out, "Content-Type: text/plain\r\n"))
			    ERROR_LOG_GOTO(ERR, "Cannot write Content-Type field");

			if(ERROR_CODE(size_t) == pstd_bio_puts(out, "Content-Length: 0\r\n"))
			    ERROR_LOG_GOTO(ERR, "Cannot write the content length");

			if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
			    ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

			if(NULL != ctx->opts.server_name && ERROR_CODE(size_t) == pstd_bio_puts(out, ctx->opts.server_name))
			    ERROR_LOG_GOTO(ERR, "Cannot write the server name field");

			if(ERROR_CODE(size_t) == pstd_bio_puts(out, "Location: "))
			    ERROR_LOG_GOTO(ERR, "Cannot write the location field");

			if(ERROR_CODE(size_t) == pstd_bio_puts(out, target))
			    ERROR_LOG_GOTO(ERR, "Cannot write the location field");

			if(ERROR_CODE(size_t) == pstd_bio_puts(out, "\r\n\r\n"))
				ERROR_LOG_GOTO(ERR, "Cannot write the request trailer");

			/* Since we have no body at this time, so we just jump to the proxy return */
			goto PROXY_RET;
		}
	}


	/* Step0: Check if we got a proxy response */
	if(ctx->opts.reverse_proxy)
	{
		int has_no_proxy;

		if(ERROR_CODE(int) == (has_no_proxy = pipe_eof(ctx->p_proxy)))
		    ERROR_LOG_GOTO(ERR, "Cannot check if we have reverse proxy response");

		if(!has_no_proxy)
		{
			scope_token_t scope = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, type_inst, ctx->a_proxy_token);
			if(ERROR_CODE(int) == pstd_bio_write_scope_token(out, scope))
			{
				const char* default_503 = "<html><body><center><h1>Service Unavailable</h1></center><hr/></body></html>";
				if(ERROR_CODE(scope_token_t) == (body_token = _write_error_page(out, 503, &ctx->opts.err_503, default_503)))
				    ERROR_LOG_GOTO(ERR, "Cannot write HTTP 503 response");

				if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
				    ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

				goto RET;
			}
			else goto PROXY_RET;
		}
	}

	/* Step1: Dtermine the encoding algorithm, size etc... */

	if(ERROR_CODE(uint32_t) == (body_flags = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, type_inst, ctx->a_body_flags)))
	    ERROR_LOG_GOTO(ERR, "Cannot read the body flag");

	if(ERROR_CODE(uint32_t) == (algorithm = _determine_compression_algorithm(ctx, type_inst, (body_flags & ctx->BODY_CAN_COMPRESS) > 0)))
	    ERROR_LOG_GOTO(ERR, "Cannot determine the encoding algorithm");

	if(ERROR_CODE(scope_token_t) == (body_token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, type_inst, ctx->a_body_token)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the request body RLS token");

	if(body_token != 0)
	{
		if(0);
#ifdef HAS_ZLIB
		else if((algorithm & _ENCODING_GZIP))
		{
			if(ERROR_CODE(scope_token_t) == (body_token = zlib_token_encode(body_token, ZLIB_TOKEN_FORMAT_GZIP, ctx->opts.compress_level)))
			    ERROR_LOG_GOTO(ERR, "Cannot encode the body with GZIP encoder");
			else
			    body_flags |= ctx->BODY_SIZE_UNKNOWN;
		}
		else if((algorithm & _ENCODING_DEFLATE))
		{
			if(ERROR_CODE(scope_token_t) == (body_token = zlib_token_encode(body_token, ZLIB_TOKEN_FORMAT_DEFLATE, ctx->opts.compress_level)))
			    ERROR_LOG_GOTO(ERR, "Cannot encode the body with Deflate encoder");
			else
			    body_flags |= ctx->BODY_SIZE_UNKNOWN;
		}
#endif
#ifdef HAS_BROTLI
		else if((algorithm & _ENCODING_BR))
		{
			/* TODO: Brotli support */
		}
#endif

		if((body_flags & ctx->BODY_SIZE_UNKNOWN) && ctx->opts.chunked_enabled)
		    algorithm |= _ENCODING_CHUNKED;

		if((algorithm & _ENCODING_CHUNKED))
		{
			if(ERROR_CODE(scope_token_t) == (body_token = chunked_encode(body_token, ctx->opts.max_chunk_size)))
			    ERROR_LOG_GOTO(ERR, "Cannot encode body with chunked encoder");
			else
			    body_flags |= ctx->BODY_SIZE_UNKNOWN;
		}
	}

	if(!(body_flags & ctx->BODY_SIZE_UNKNOWN))
	{
		if(ERROR_CODE(uint64_t) == (body_size = PSTD_TYPE_INST_READ_PRIMITIVE(uint64_t, type_inst, ctx->a_body_size)))
		    ERROR_LOG_GOTO(ERR, "Cannot determine the size of the body");
	}
	else if(!(algorithm & _ENCODING_CHUNKED))
	{
		const char* default_406 = "<html><body><h1>Content Encoding Not Acceptable</h1></body></html>";
		if(ERROR_CODE(scope_token_t) == (body_token = _write_error_page(out, 406, &ctx->opts.err_406, default_406)))
		    ERROR_LOG_GOTO(ERR, "Cannot write the HTTP 500 response");

		if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
		    ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

		goto RET;
	}


	/* Step 2: Write the status line */
	if(ERROR_CODE(uint16_t) == (status_code = PSTD_TYPE_INST_READ_PRIMITIVE(uint16_t, type_inst, ctx->a_status_code)))
	    ERROR_LOG_GOTO(ERR, "Cannot read the status code from response pipe");

	if(ERROR_CODE(int) == _write_status_line(out, status_code))
	    ERROR_LOG_GOTO(ERR, "Cannot write the status code");


	/* Write the content type */
	if(ERROR_CODE(int) == _write_string_field(out, type_inst, ctx->a_mime_type, "Content-Type: ", "application/octet-stream" ))
	    ERROR_LOG_GOTO(ERR, "Cannot write the mime type");

	/* Write redirections */
	if(status_code == 301 || status_code == 302 || status_code == 308 || status_code == 309)
	{
		if(ERROR_CODE(int) == _write_string_field(out, type_inst, ctx->a_redir_loc, "Location: ", "/"))
		    ERROR_LOG_GOTO(ERR, "Cannot write the redirect location");
	}

	/* Write the encoding fields */
	if(ERROR_CODE(int) == _write_encoding(out, algorithm, body_size))
	    ERROR_RETURN_LOG(int, "Cannot write the encoding fields");

	/* Write the connection field */
	if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
	    ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

	if((body_flags & ctx->BODY_SEEKABLE) && ERROR_CODE(size_t) == pstd_bio_puts(out, "Accept-Ranges: bytes\r\n"))
	    ERROR_LOG_GOTO(ERR, "Cannot write the accept-ranges header");

	if((body_flags & ctx->BODY_RANGED))
	{
		size_t left, right, total;
		if(ERROR_CODE(size_t) == (left = PSTD_TYPE_INST_READ_PRIMITIVE(size_t, type_inst, ctx->a_range_begin)))
		    ERROR_LOG_GOTO(ERR, "Cannot read the range begin");
		if(ERROR_CODE(size_t) == (right = PSTD_TYPE_INST_READ_PRIMITIVE(size_t, type_inst, ctx->a_range_end)))
		    ERROR_LOG_GOTO(ERR, "Cannot read the range end");
		if(ERROR_CODE(size_t) == (total = PSTD_TYPE_INST_READ_PRIMITIVE(size_t, type_inst, ctx->a_range_total)))
		    ERROR_LOG_GOTO(ERR, "Cannot read the total size");
		if(ERROR_CODE(size_t) == pstd_bio_printf(out, "Content-Range: bytes %zu-%zu/%zu\r\n", left, right - 1, total))
		    ERROR_LOG_GOTO(ERR, "Cannot write the content-range header");
	}

RET:

	/* Write the server name */
	if(NULL != ctx->opts.server_name && ERROR_CODE(size_t) == pstd_bio_puts(out, ctx->opts.server_name))
	    ERROR_LOG_GOTO(ERR, "Cannot write the server name field");

	/* Write the body deliminators */
	if(ERROR_CODE(size_t) == pstd_bio_puts(out, "\r\n"))
	    ERROR_RETURN_LOG(int, "Cannot write the body deliminator");

	/* Write the body */
	if(body_token != 0 && ERROR_CODE(int) == pstd_bio_write_scope_token(out, body_token))
	    ERROR_LOG_GOTO(ERR, "Cannot write the body content");

PROXY_RET:

	if(ERROR_CODE(int) == pstd_type_instance_free(type_inst))
	    ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	if(ERROR_CODE(int) == pstd_bio_free(out))
	    ERROR_RETURN_LOG(int, "Cannot dispose the BIO object");

	return 0;
ERR:
	if(NULL != type_inst) pstd_type_instance_free(type_inst);
	if(NULL != out) pstd_bio_free(out);

	return ERROR_CODE(int);
}

SERVLET_DEF = {
	.desc    = "HTTP Response Render",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};
