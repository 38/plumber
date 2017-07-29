/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>
#include <proto.h>

/**
 * @brief Describe a modification operation
 **/
typedef struct {
	pipe_t                 input;       /*!< The input pipe where we takes the data that needs to be written */
	const char*            actual_type; /*!< The actual type of the pipe */
	const char*            field_name;  /*!< The field name we want to modify in the base input */
	pstd_type_accessor_t   accessor;    /*!< The accessor we used to access the related field */
	uint32_t               validated:1; /*!< If the type of the pipe has been validated */
} modification_t;

/**
 * @brief The servlet context
 **/
typedef struct {
	pipe_t                base;      /*!< The base we want to modify */
	const char*           base_type; /*!< The base type */
	pipe_t                output;    /*!< The output pipe */
	pstd_type_model_t*    type_model;/*!< The type model */
	size_t                count;     /*!< The number of fields that needs to be changed */
	modification_t*       modifications;  /*!< The modification we needs to performe */
} context_t;

static int _type_assert(pipe_t pipe, const char* type, const void* data)
{
	context_t* ctx = (context_t*)data;
	if(pipe == ctx->base)
		ctx->base_type = type;
	else 
	{
		uint32_t i;
		for(i = 0; i < ctx->count && pipe != ctx->modifications[i].input; i ++);

		if(i == ctx->count)
			ERROR_RETURN_LOG(int, "Cannot match the pipe descriptor");

		ctx->modifications[i].actual_type = type;
	}

	uint32_t i;
	for(i = 0; i < ctx->count && ctx->base_type != NULL; i ++)
		if(ctx->modifications[i].actual_type != NULL && !ctx->modifications[i].validated)
		{
			const char* from_type = ctx->modifications[i].actual_type;
			const char* to_type   = proto_db_field_type(ctx->base_type, ctx->modifications[i].field_name);
			if(NULL == from_type || NULL == to_type)
				ERROR_RETURN_LOG(int, "Cannot get either from type or to type");

			const char* type_array[] = {from_type, to_type, NULL};

			if(proto_db_common_ancestor(type_array) != to_type)
				ERROR_RETURN_LOG(int, "Type error: from type %s and to type [%s.%s] = %s is not compitable", 
						         from_type, ctx->base_type, ctx->modifications[i].field_name, to_type);
			ctx->modifications[i].validated = 1;
		}
	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	ctx->base_type = NULL;
	ctx->type_model = NULL;
	ctx->modifications = NULL;
	ctx->count = 0;
	ctx->base_type = NULL;
	ctx->modifications = NULL;
	
	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create new type model");

	if(ERROR_CODE(pipe_t) == (ctx->base = pipe_define("base", PIPE_INPUT, "$T")))
		ERROR_RETURN_LOG(int, "Cannot define the base input pipe");
	
	if(ERROR_CODE(int) == pstd_type_model_assert(ctx->type_model, ctx->base, _type_assert, ctx))
		ERROR_RETURN_LOG(int, "Cannot install the type assertion callback");

	ctx->count = argc - 1;


	if(NULL == (ctx->modifications = calloc(1, sizeof(modification_t) * ctx->count)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the modification array");


	uint32_t i;
	for(i = 0; i < ctx->count; i ++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "$M_%d", i);
		if(ERROR_CODE(pipe_t) == (ctx->modifications[i].input = pipe_define(argv[i + 1], PIPE_INPUT, buf)))
			ERROR_RETURN_LOG(int, "Cannot define pipe for field %s", argv[i + 1]);
		ctx->modifications[i].field_name = argv[i + 1];
		
		if(ERROR_CODE(int) == pstd_type_model_assert(ctx->type_model, ctx->modifications[i].input, _type_assert, ctx))
			ERROR_RETURN_LOG(int, "Cannot install the type assertion callback");

		if(ERROR_CODE(pstd_type_accessor_t) == (ctx->modifications[i].accessor = pstd_type_model_get_accessor(ctx->type_model, ctx->base, argv[i + 1])))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for the field %s", argv[i + 1]);
	}

	return 0;
}

static int _cleanup(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	int rc = 0;
	if(NULL != ctx->modifications) free(ctx->modifications);
	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);
	return rc;
}

SERVLET_DEF = {
	.desc = "The servlet used to modify fields on the fly",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _cleanup
};
