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
	const char*            expected_type; /*!< The expected type name for this modification */
	char*                  field_name;  /*!< The field name we want to modify in the base input, this is a copy of the param */
	uint32_t               offset;      /*!< The offset where the field begins */
	uint32_t               size;        /*!< The size of the field */
	uint32_t               validated:1; /*!< If the type of the pipe has been validated */
} modification_t;

/**
 * @brief The servlet context
 **/
typedef struct {
	pipe_t                base;      /*!< The base we want to modify */
	const char*           base_type; /*!< The base type */
	pipe_t                output;    /*!< The output pipe */
	uint32_t              base_size; /*!< The size of the base type */
	uint32_t              count;     /*!< The number of fields that needs to be changed */
	modification_t*       modifications;  /*!< The modification we needs to performe */
} context_t;

static int _on_type_determined(pipe_t pipe, const char* type, void* data)
{
	int ret = ERROR_CODE(int);
	if(ERROR_CODE(int) == proto_init())
		ERROR_RETURN_LOG(int, "Cannot initialize the libproto");

	context_t* ctx = (context_t*)data;
	if(pipe == ctx->base)
	{
		ctx->base_type = type;
		if(ERROR_CODE(uint32_t) == (ctx->base_size = proto_db_type_size(type)))
			ERROR_RETURN_LOG(int, "Cannot get the size of the base type %s", type);

		uint32_t i;
		for(i = 0; i < ctx->count; i ++)
		{
			modification_t* mod = ctx->modifications + i;
			if(NULL == (mod->expected_type = proto_db_field_type(type, mod->field_name)))
				ERROR_RETURN_LOG(int, "Cannot get the type of field %s.%s", type, mod->field_name);

			if(ERROR_CODE(uint32_t) == (mod->offset = proto_db_type_offset(type, mod->field_name, &mod->size)))
				ERROR_RETURN_LOG(int, "Cannot get the offset of the field %s.%s", type, mod->field_name);
		}
	}
	else 
	{
		uint32_t i;
		for(i = 0; i < ctx->count && pipe != ctx->modifications[i].input; i ++);

		if(i == ctx->count)
			ERROR_LOG_GOTO(EXIT, "Cannot match the pipe descriptor");

		ctx->modifications[i].actual_type = type;
	}

	uint32_t i;
	for(i = 0; i < ctx->count; i ++)
		if(ctx->modifications[i].actual_type != NULL &&
		   ctx->modifications[i].expected_type != NULL && 
		   !ctx->modifications[i].validated)
		{
			const char* from_type = ctx->modifications[i].actual_type;
			const char* to_type   = ctx->modifications[i].expected_type;
			if(NULL == from_type || NULL == to_type)
				ERROR_LOG_GOTO(EXIT, "Cannot get either from type or to type");

			const char* type_array[] = {from_type, to_type, NULL};

			if(proto_db_common_ancestor(type_array) != to_type)
				ERROR_LOG_GOTO(EXIT, "Type error: from type %s and to type [%s.%s] = %s is not compitable", 
						       from_type, ctx->base_type, ctx->modifications[i].field_name, to_type);
			ctx->modifications[i].validated = 1;
		}

	if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("output", PIPE_OUTPUT, "$T")))
		ERROR_LOG_GOTO(EXIT, "Cannot define the output pipe");

	ret = 0;
EXIT:

	if(ERROR_CODE(int) == proto_finalize())
		ERROR_RETURN_LOG(int, "Cannot finalize the libproto");
	return ret;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	uint32_t i;
	context_t* ctx = (context_t*)ctxbuf;
	ctx->base_type = NULL;
	ctx->modifications = NULL;
	ctx->count = argc - 1;
	ctx->base_type = NULL;
	ctx->modifications = NULL;
	
	if(ERROR_CODE(pipe_t) == (ctx->base = pipe_define("base", PIPE_INPUT, "$BASE")))
		ERROR_RETURN_LOG(int, "Cannot define the base input pipe");
	
	if(ERROR_CODE(int) == pipe_set_type_callback(ctx->base, _on_type_determined, ctx))
		ERROR_RETURN_LOG(int, "Cannot setup the on type determined callback function for the base input");

	if(NULL == (ctx->modifications = calloc(1, sizeof(modification_t) * ctx->count)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the modification array");

	for(i = 0; i < ctx->count; i ++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "$M_%d", i);
		if(ERROR_CODE(pipe_t) == (ctx->modifications[i].input = pipe_define(argv[i + 1], PIPE_INPUT, buf)))
			ERROR_RETURN_LOG(int, "Cannot define pipe for field %s", argv[i + 1]);
		if(NULL == (ctx->modifications[i].field_name = strdup(argv[i + 1])))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate string %s", argv[i + 1]);

		if(ERROR_CODE(int) == pipe_set_type_callback(ctx->modifications[i].input, _on_type_determined, ctx))
			ERROR_RETURN_LOG(int, "Cannot setup the on type determined callback function for input pipe for %s", argv[i + 1]);
	}

	if(ERROR_CODE(pipe_t) == (ctx->base = pipe_define("output", PIPE_OUTPUT, "$BASE")))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe");

	return 0;
}

static int _cleanup(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	if(NULL != ctx->modifications) 
	{
		uint32_t i;
		for(i = 0; i < ctx->count; i ++)
			if(ctx->modifications[i].field_name != NULL)
				free(ctx->modifications[i].field_name);
		free(ctx->modifications);
	}

	return 0;
}
#if 0
static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	size_t ti_size = pstd_type_instance_size(ctx->type_model);
	if(ERROR_CODE(size_t) == ti_size)
		ERROR_RETURN_LOG(int, "Cannot get the size of the type instance");

	char ti_buf[ti_size];
	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->type_model, ti_buf);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create new type instance");

	uint32_t i;
	for(i = 0; i < ctx->count; i ++)
	{
		const modification_t* mod = ctx->modifications + i;
		size_t size = pstd_type_instance_field_size(inst, mod->accessor);
		if(ERROR_CODE(size_t) == size)
			ERROR_LOG_GOTO(ERR, "Cannot get the size of the accessor");
		char buf[size];

		size_t bytes_to_read = size;

		while(bytes_to_read > 0)
		{
			int rc = pipe_eof(mod->input);
			if(ERROR_CODE(int) == rc) ERROR_LOG_GOTO(ERR, "Cannot check if the pipe contains data");
			if(rc) break;

			size_t bytes_read = pipe_hdr_read(mod->input, buf + size - bytes_to_read, bytes_to_read);
			if(ERROR_CODE(size_t) == bytes_read) ERROR_LOG_GOTO(ERR, "Cannot read header from the input");
		}

		if(bytes_to_read == size) continue;  /* We need ignore the pipes that is totally empty */
		else if(bytes_to_read != size) ERROR_LOG_GOTO(ERR, "Incomplete typed header");

	}
}
#endif

SERVLET_DEF = {
	.desc = "The servlet used to modify fields on the fly",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _cleanup
};
