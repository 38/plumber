/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>

#include <options.h>
#include <chuncked.h>

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

static int _unload(void* ctxmem)
{
	int rc = 0;
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);
	return rc;
}

static inline uint32_t _determine_compression_algorithm(ctx_t *ctx, pstd_type_instance_t* inst)
{
	scope_token_t accept_token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->a_accept);

	if(ERROR_CODE(scope_token_t) == accept_token)
		ERROR_RETURN_LOG(uint32_t, "Cannot read the accept token from pipe");

	const pstd_string_t* accept_obj = pstd_string_from_rls(accept_token);

	if(NULL == accept_obj)
		ERROR_RETURN_LOG(uint32_t, "Cannot get the RLS string object for the token");

	const char* accepts = pstd_string_value(accept_obj);
	const char* accepts_end = accepts + pstd_string_length(accept_obj);

	if(NULL == accepts)
		ERROR_RETURN_LOG(uint32_t, "Cannot get the accepts string");

	unsigned current_len = 0;
	const char* ptr;
	for(ptr = accepts; *ptr; ptr ++)
	{
		if(current_len == 0)
		{
			if(*ptr == ' ' || *ptr == '\t')
				continue;
			else
			{
				switch(*ptr)
				{
					case 'g':
						/* gzip */
						if(ctx->opt.gzip && accepts_end - ptr >= 4 && memcmp("gzip", ptr, 4) == 0)
							return ctx->encode_method.ENCODE_METHOD_GZIP;
						break;
					case 'd':
						/* deflate */
						if(ctx->opt.deflate && accepts_end - ptr >= 7 && memcmp("deflate", ptr, 7) == 0)
							return ctx->encode_method.ENCODE_METHOD_DEFLATE;
						break;
					case 'c':
						/* compress */
						if(ctx->opt.compress && accepts_end - ptr >= 8 && memcmp("compress", ptr, 8) == 0)
							return ctx->encode_method.ENCODE_METHOD_COMPRESS;
						break;
					case 'b':
						/* br */
						if(ctx->opt.br && accepts_end - ptr >= 2 && memcmp("br", ptr, 2) == 0)
							return ctx->encode_method.ENCODE_METHOD_BR;
						break;
					default:
						if(NULL == (ptr = strchr(ptr, ',')))
							return ctx->encode_method.ENCODE_METHOD_IDENTITY;
				}
			}
		}
	}

	return ctx->encode_method.ENCODE_METHOD_IDENTITY;
}

static int _exec(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	pstd_type_instance_t* type_inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == type_inst) 
		ERROR_RETURN_LOG(int, "Cannot  create type instance");

	/* TODO: also consider the mime type as well */
	
	scope_token_t body = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, type_inst, ctx->a_body);
	if(ERROR_CODE(scope_token_t) == body)
		ERROR_LOG_GOTO(ERR, "Cannot read the body token");

	uint32_t algorithm = _determine_compression_algorithm(ctx, type_inst);

	if(ERROR_CODE(uint32_t) == algorithm)
		ERROR_LOG_GOTO(ERR, "Cannot determine the compression algorithm");

	uint64_t size = ctx->SIZE_UNKNOWN;
	
	if(ctx->encode_method.ENCODE_METHOD_IDENTITY == algorithm)
	{
		/* TODO: we need to determine the actual size */
	}
	
	if(size == ctx->SIZE_UNKNOWN)
	{
		if(ctx->opt.chuncked)
		{
			algorithm |= ctx->encode_method.ENCODE_METHOD_CHUNCKED;

			body = chuncked_encode(body, 4);
			if(ERROR_CODE(scope_token_t) == body)
				ERROR_LOG_GOTO(ERR, "Cannot encode the body");
		}
		else
		{
			/* TODO: what if we cannot use chuncked? */
		}
	}

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_encode_method, algorithm))
		ERROR_LOG_GOTO(ERR, "Cannot write the algorithm field to output");
	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_encode_size, size))
		ERROR_LOG_GOTO(ERR, "Cannot write the size field to output");
	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_encode_token, body))
		ERROR_LOG_GOTO(ERR, "Cannot write the encoded token to output");

	if(ERROR_CODE(int) == pstd_type_instance_free(type_inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	return 0;

ERR:
	pstd_type_instance_free(type_inst);
	return ERROR_CODE(int);
}


SERVLET_DEF = {
	.desc    = "The HTTP body encoder",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};
