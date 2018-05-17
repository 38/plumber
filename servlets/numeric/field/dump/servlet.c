/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>

#include <psnl.h>

#include <options.h>

typedef struct {
	options_t              options;   /*!< The servlet options */

	pipe_t                 p_field;   /*!< The pipe for the field */
	pipe_t                 p_dump;    /*!< The dump of the field */

	pstd_type_model_t*     type_model;/*!< The servlet type model */

	pstd_type_accessor_t   a_field_tok; /*!< The accessor for the field token */

	psnl_cpu_field_type_info_t field_type;  /*!< The field type description */
} ctx_t;

static int _field_type_assert(pipe_t pipe, const char* typename, void* data)
{
	(void)pipe;

	ctx_t* ctx = (ctx_t*)data;

	if(ERROR_CODE(int) == psnl_cpu_field_type_parse(typename, &ctx->field_type))
		ERROR_RETURN_LOG(int, "Cannot parse the typename as a field type");

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int,  "Cannot parse the servlet init param");

	PIPE_LIST(pipes)
	{
		PIPE("field", PIPE_INPUT,  "$T",               ctx->p_field),
		PIPE("dump",  PIPE_OUTPUT, "plumber/base/Raw", ctx->p_dump) 
	};

	if(ERROR_CODE(int) == PIPE_BATCH_INIT(pipes))
		ERROR_RETURN_LOG(int, "Cannot initialize the pipe");

	PSTD_TYPE_MODEL(type_model)
	{
		PSTD_TYPE_MODEL_FIELD(ctx->p_field,     token,       ctx->a_field_tok)
	};

	if(NULL == (ctx->type_model = PSTD_TYPE_MODEL_BATCH_INIT(type_model)))
		ERROR_RETURN_LOG(int, "Cannot create the type model for this servlet");

	if(ERROR_CODE(int) == pstd_type_model_assert(ctx->type_model, ctx->p_field, _field_type_assert, ctx))
		ERROR_RETURN_LOG(int, "Cannot setup the type assertion callback");

	return 0;
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
	.desc = "Dump the field to either raw pipe or a RLS string object",
	.version = 0x0,
	.size = sizeof(ctx_t),
	.init = _init,
	.unload = _unload
};
