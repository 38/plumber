/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <mime.h>
#include <options.h>
#include <input.h>
#include <file.h>
#include <http.h>
#include <raw.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t          options;       /*!< The servlet options */
	pstd_type_model_t* type_model;    /*!< The type model */

	input_ctx_t*       input_ctx;     /*!< The input context */

	union {
		void*          out_ctx;       /*!< The generic pointer to output context */
		file_ctx_t*    file_ctx;      /*!< The file context */
		http_ctx_t*    http_ctx;      /*!< The HTTP context */
		raw_ctx_t*     raw_ctx;       /*!< The RAW context */
	};
} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	memset(ctx, 0, sizeof(*ctx));

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot initialize the type model");

	if(NULL == (ctx->input_ctx = input_ctx_new(&ctx->options, ctx->type_model)))
		ERROR_RETURN_LOG(int, "Cannot initialize the input context");

	switch(ctx->options.output_mode)
	{
		case OPTIONS_OUTPUT_MODE_FILE:
			ctx->file_ctx = file_ctx_new(&ctx->options, ctx->type_model);
			break;
		case OPTIONS_OUTPUT_MODE_RAW:
			ctx->raw_ctx = raw_ctx_new(&ctx->options, ctx->type_model);
			break;
		case OPTIONS_OUTPUT_MODE_HTTP:
			ctx->http_ctx = http_ctx_new(&ctx->options, ctx->type_model);
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid output mode");
	}

	if(ctx->out_ctx == NULL)
		ERROR_RETURN_LOG(int, "Cannot create output context");

	return 0;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;

	if(ERROR_CODE(int) == options_free(&ctx->options))
		rc = ERROR_CODE(int);

	if(NULL != ctx->input_ctx && ERROR_CODE(int) == input_ctx_free(ctx->input_ctx))
		rc = ERROR_CODE(int);

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);

	if(NULL != ctx->out_ctx)
	{
		switch(ctx->options.output_mode)
		{
			case OPTIONS_OUTPUT_MODE_FILE:
				if(ERROR_CODE(int) == file_ctx_free(ctx->file_ctx))
					rc = ERROR_CODE(int);
				break;
			case OPTIONS_OUTPUT_MODE_RAW:
				if(ERROR_CODE(int) == raw_ctx_free(ctx->raw_ctx))
					rc = ERROR_CODE(int);
				break;
			case OPTIONS_OUTPUT_MODE_HTTP:
				if(ERROR_CODE(int) == http_ctx_free(ctx->http_ctx))
					rc = ERROR_CODE(int);
				break;
			default:
				ERROR_RETURN_LOG(int, "Invalid output mode");
		}
	}

	return rc;
}

static int _exec(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == inst)
		ERROR_RETURN_LOG(int, "Cannot create type instance");

	char buf[PATH_MAX + 1];
	const char* extname = NULL;
	size_t length;

	if(ERROR_CODE(size_t) == (length = input_ctx_read_path(ctx->input_ctx, inst, buf, sizeof(buf), &extname)))
		ERROR_LOG_GOTO(ERR, "Cannot read path from the input");

	int rc = 0;

	switch(ctx->options.output_mode)
	{
		case OPTIONS_OUTPUT_MODE_FILE:
			rc = file_ctx_exec(ctx->file_ctx, inst, buf);
			break;
		case OPTIONS_OUTPUT_MODE_RAW:
			rc = raw_ctx_exec(ctx->raw_ctx, inst, buf);
			break;
		case OPTIONS_OUTPUT_MODE_HTTP:
			rc = http_ctx_exec(ctx->http_ctx, inst, buf, extname);
			break;
		default:
				ERROR_LOG_GOTO(ERR, "Invalid output mode");
	}

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	return rc;
ERR:
	pstd_type_instance_free(inst);
	return ERROR_CODE(int);
}

SERVLET_DEF = {
	.desc    = "Reads a file from disk",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};

