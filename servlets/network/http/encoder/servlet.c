/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t opt;            /*!< The options for this servlet instance */
	pipe_t    p_type;         /*!< The content type pipe */
	pipe_t    p_accept;       /*!< What encoding the client accepts */
	pipe_t    p_body;         /*!< The body pipe */
	pipe_t    p_result;       /*!< The result pipe */

	pstd_type_model_t* type_model;   /*!< The type model for this servlet */

	pstd_type_accessor_t a_type;           /*!< The accessor for type string */
	pstd_type_accessor_t a_accept;         /*!< The accessor for accept string */
	pstd_type_accessor_t a_body;           /*!< The accessor for the body */
	pstd_type_accessor_t a_encode_method;  /*!< The accessor for encode method */
	pstd_type_accessor_t a_encode_token;   /*!< The accessor for the encoded token */
	pstd_type_accessor_t a_encode_size;    /*!< The size of the encoded body */

	enum {
		_TYPE_FILE,                   /*!< If this is a RLS file object */
		_TYPE_STRING,                 /*!< If this is a RLS string object */
		_TYPE_GENERIC                 /*!< If this is a generic RLS token */
	}                    body_type;   /*!< The body type */

	struct {
		uint32_t ENCODE_METHOD_IDENTITY;          /*!< No encoder applied */
		uint32_t ENCODE_METHOD_GZIP;              /*!< Gzip encoded */
		uint32_t ENCODE_METHOD_COMPRESS;          /*!< Compress encoded */
		uint32_t ENCODE_METHOD_BR;                /*!< BR Encoded */
		uint32_t ENCODE_METHOD_DEFLATE;           /*!< Deflate encoded */
		uint32_t ENCODE_METHOD_CHUNCKED;          /*!< Chuncked encoded */
	}                    encode_method;  /*!< The constants for the encode method */

	uint64_t SIZE_UNKNOWN;    /*!< The place holder for the unknown size */
} ctx_t;

static int _detect_body_type(pipe_t pipe, const char* type, void* data)
{
	(void)pipe;
	ctx_t* ctx = (ctx_t*)data;

	if(strcmp(type, "plumber/std/request_local/String") == 0)
		ctx->body_type = _TYPE_STRING;
	else if(strcmp(type, "plumber/std/request_local/File") == 0)
		ctx->body_type = _TYPE_FILE;
	else 
		ctx->body_type = _TYPE_GENERIC;

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	memset(ctx, 0, sizeof(*ctx));

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->opt))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	if(ERROR_CODE(pipe_t) == (ctx->p_type = pipe_define("type", PIPE_INPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot define the type input port");

	if(ERROR_CODE(pipe_t) == (ctx->p_accept = pipe_define("accept", PIPE_INPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot define the accept input port");

	if(ERROR_CODE(pipe_t) == (ctx->p_body = pipe_define("body", PIPE_INPUT, "$T")))
		ERROR_RETURN_LOG(int, "Cannot define the body pipe");

	if(ERROR_CODE(pipe_t) == (ctx->p_result = pipe_define("result", PIPE_OUTPUT, "plumber/std_servlet/network/http/encoder/v0/Encoded")))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create type model for the servlet");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_type = pstd_type_model_get_accessor(ctx->type_model, ctx->p_type, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for type.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_accept = pstd_type_model_get_accessor(ctx->type_model, ctx->p_accept, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for accept.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_body = pstd_type_model_get_accessor(ctx->type_model, ctx->p_body, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for body.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_encode_method = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "method")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for result.method");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_encode_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for  result.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_encode_size = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "size")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for result.size");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "IDENTITY", &ctx->encode_method.ENCODE_METHOD_IDENTITY))
		ERROR_RETURN_LOG(int, "Cannot get constant result.IDENTITY");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "GZIP", &ctx->encode_method.ENCODE_METHOD_GZIP))
		ERROR_RETURN_LOG(int, "Cannot get constant result.GZIP");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "COMPRESS", &ctx->encode_method.ENCODE_METHOD_COMPRESS))
		ERROR_RETURN_LOG(int, "Cannot get constant result.COMPRESS");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "BR", &ctx->encode_method.ENCODE_METHOD_BR))
		ERROR_RETURN_LOG(int, "Cannot get constant result.BR");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "DEFLATE", &ctx->encode_method.ENCODE_METHOD_DEFLATE))
		ERROR_RETURN_LOG(int, "Cannot get constant result.DEFLATE");
	
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "CHUNCKED", &ctx->encode_method.ENCODE_METHOD_CHUNCKED))
		ERROR_RETURN_LOG(int, "Cannot get constant result.CHUNCKED");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "SIZE_UNKNOWN", &ctx->SIZE_UNKNOWN))
		ERROR_RETURN_LOG(int, "Cannot get constant result.SIZE_UNKNOWN");

	if(ERROR_CODE(int) == pstd_type_model_assert(ctx->type_model, ctx->p_body, _detect_body_type, ctx))
		ERROR_RETURN_LOG(int, "Cannot detect the body type");

	return 0;
}


SERVLET_DEF = {
	.desc    = "The HTTP body encoder",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init
};
