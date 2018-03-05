/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>
#include <mime.h>

/**
 * @brief The context used by the HTTP output mode
 **/
typedef struct {
	mime_map_t*              mime_map;        /*!< The mime type map */

	char const**             index_names;     /*!< The possible index names */
	size_t*                  index_name_len;  /*!< The length of the index name */

	pstd_type_accessor_t     a_status_code;   /*!< The accessor for the status code */
	pstd_type_accessor_t     a_body_flags;    /*!< The accessor for the body flags */
	pstd_type_accessor_t     a_body_size;     /*!< The accessor for the body size */
	pstd_type_accessor_t     a_body_token;    /*!< The accessor for the body RLS token */
	pstd_type_accessor_t     a_redir_token;   /*!< The accessor for the redirect target token */
	pstd_type_accessor_t     a_mime_token;    /*!< The accessor for the mime type RLS */

	uint32_t                 HTTP_OK;         /*!< The HTTP OK status code */
	uint32_t                 HTTP_FORBIDEN;   /*!< The HTTP forbiden status code */
	uint32_t                 HTTP_NOT_FOUND;  /*!< The HTTP not found status code */
	uint32_t                 HTTP_MOVED;      /*!< The HTTP moved status code */

	uint32_t                 BODY_CAN_COMPRESS; /*!< The body flag that indicates this file can be compressed */
} http_ctx_t;

/**
 * @brief The context for the file output mode
 **/
typedef struct {
	pstd_type_accessor_t     a_file_token;    /*!< The file object RLS token */
} file_ctx_t;

/**
 * @brief The context for the raw output mode
 **/
typedef struct {
} raw_ctx_t;

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t                opts;         /*!< The servlet options */
	pipe_t                   p_path;       /*!< The input pipe for the path to the file we want to read */
	pipe_t                   p_result;     /*!< The output pipe for the result read from disk */
	pstd_type_model_t*       type_model;   /*!< The type model for this servlet */
	pstd_type_accessor_t     a_path_token; /*!< The Accessor for the RLS token */
	union {
		http_ctx_t           http;         /*!< The HTTP output context */
		file_ctx_t           file;         /*!< The file output context */
		raw_ctx_t            raw;          /*!< The Raw output context */
	}                        out_ctx;      /*!< The context for the output */
} ctx_t;

static inline int _init_http(ctx_t* ctx)
{
	if(ERROR_CODE(pipe_t) == (ctx->p_result = pipe_define("result", PIPE_OUTPUT, "plumber/std_servlet/network/http/render/v0/Response")))
		ERROR_RETURN_LOG(int, "Cannot declare the result pipe port");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_ctx.http.a_status_code = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "status.status_code")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for result.status.status_code");
	
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_ctx.http.a_body_flags = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "body_flags")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for result.body_flags");
	
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_ctx.http.a_body_size = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "body_size")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for result.body_size");
	
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_ctx.http.a_body_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "body_token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for result.body_token");
	
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_ctx.http.a_redir_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "redirect_location.token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for result.body_flags");
	
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_ctx.http.a_mime_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "mime_type.token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for result.body_flags");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "status.OK", &ctx->out_ctx.http.HTTP_OK))
		ERROR_RETURN_LOG(int, "Cannot get the HTTP OK status code");
	
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "status.NOT_FOUND", &ctx->out_ctx.http.HTTP_NOT_FOUND))
		ERROR_RETURN_LOG(int, "Cannot get the HTTP OK status code");
	
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "status.FORBIDEN", &ctx->out_ctx.http.HTTP_FORBIDEN))
		ERROR_RETURN_LOG(int, "Cannot get the HTTP OK status code");
	
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->p_result, "status.MOVED_PERMANENTLY", &ctx->out_ctx.http.HTTP_MOVED))
		ERROR_RETURN_LOG(int, "Cannot get the HTTP OK status code");

	uint32_t n_index_names = 0, i;

	const char* ptr, *cur_begin = NULL;
	for(ptr = ctx->opts.dir_index_file; *ptr; ptr ++)
		if(*ptr == ';')
			n_index_names = 0;

	if(ptr > ctx->opts.dir_index_file && ptr[-1] != ';')
		n_index_names ++;

	if(NULL == (ctx->out_ctx.http.index_names = (char const* *)calloc(sizeof(char const*), n_index_names + 1)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the index name array");

	if(NULL == (ctx->out_ctx.http.index_name_len = (size_t*)calloc(sizeof(size_t*), n_index_names + 1)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the index name length array");

	for(ptr = ctx->opts.dir_index_file;; ptr ++)
		if(*ptr == ';' || *ptr == 0)
		{
			if(cur_begin != NULL && cur_begin < ptr - 1)
			{
				ctx->out_ctx.http.index_names[i] = cur_begin;
				ctx->out_ctx.http.index_name_len[i] = (size_t)(ptr - cur_begin);
				i ++;
				cur_begin = ptr + 1;
			}
			if(*ptr == 0) break;
		}

	if(NULL == (ctx->out_ctx.http.mime_map = mime_map_new(ctx->opts.mime_spec, ctx->opts.compress_list)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot create MIME type mapping object");

	return 0;
}

static inline int _init_file(ctx_t* ctx)
{
	if(ERROR_CODE(pipe_t) == (ctx->p_result = pipe_define("result", PIPE_OUTPUT, "plumber/std/request_local/File")))
		ERROR_RETURN_LOG(int, "Cannot declare the result pipe port");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_ctx.file.a_file_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_result, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for result.token");

	return 0;
}

static inline int _init_raw(ctx_t* ctx)
{
	if(ERROR_CODE(pipe_t) == (ctx->p_result = pipe_define("result", PIPE_OUTPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot declare the result pipe port");
	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->opts))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	if(ERROR_CODE(pipe_t) == (ctx->p_path = pipe_define("path", PIPE_INPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot declare the path input pipe port");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create type model for the servlet");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_path_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_path, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for path.token");

	switch(ctx->opts.output_mode)
	{
		case OPTIONS_OUTPUT_RAW:
			return _init_raw(ctx);
		case OPTIONS_OUTPUT_FILE:
			return _init_file(ctx);
		case OPTIONS_OUTPUT_HTTP:
			return _init_http(ctx);
		default:
			ERROR_RETURN_LOG(int, "Invalid output mode");
	}
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;

	if(ctx->opts.output_mode == OPTIONS_OUTPUT_HTTP)
	{
		if(NULL != ctx->out_ctx.http.mime_map && ERROR_CODE(int) == mime_map_free(ctx->out_ctx.http.mime_map))
			rc = ERROR_CODE(int);

		if(NULL != ctx->out_ctx.http.index_name_len)
			free(ctx->out_ctx.http.index_name_len);

		if(NULL != ctx->out_ctx.http.index_names)
			free(ctx->out_ctx.http.index_names);
	}

	if(ERROR_CODE(int) == options_free(&ctx->opts))
		rc = ERROR_CODE(int);

	return rc;
}

static int _exec(void* ctxmem)
{
	(void)ctxmem;

	return 0;
}

SERVLET_DEF = {
	.desc    = "Read file from disk",
	.size    = sizeof(ctx_t),
	.version = 0x0,
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};
