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

static int _cmp_modification(const void* pa, const void* pb)
{
	const modification_t* a = (modification_t*)pa;
	const modification_t* b = (modification_t*)pb;

	if(a->offset < b->offset) return -1;
	if(a->offset > b->offset) return 1;
	return 0;
}

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

	uint32_t i, validated = 0;
	for(i = 0; i < ctx->count; i ++)
	{
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
		if(ctx->modifications[i].validated) validated ++;
	}

	if(validated == ctx->count)
	{
		/* When every thing has been validated, we need to sort it */
		qsort(ctx->modifications, ctx->count, sizeof(ctx->modifications[0]), _cmp_modification);
		/* Then we need to verify the areas the modifcation writes are not overlapping */
		for(i = 0; i < ctx->count - 1; i ++)
		{
			uint32_t end = ctx->modifications[i].offset + ctx->modifications[i].size;
			if(end > ctx->modifications[i + 1].offset) 
				ERROR_LOG_GOTO(EXIT, "The area of the modification areas are overlapped");
		}
	}

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

static inline int _copy_header(pipe_t from, pipe_t to, uint32_t size)
{
	char buf[size];
	size_t bytes_to_copy = size;

	int eof_rc = pipe_eof(from);
	if(ERROR_CODE(int) == eof_rc) ERROR_RETURN_LOG(int, "Cannot check if the from segment contains data");
	/* This means we just leave the to pipe there */
	if(eof_rc) return 0;

	while(bytes_to_copy > 0)
	{
		size_t bytes_read = pipe_hdr_read(from, buf, bytes_to_copy);
		if(ERROR_CODE(size_t) == bytes_read)
			ERROR_RETURN_LOG(int, "Cannot read header from the input pipe");

		if(bytes_read == 0)
		{
			eof_rc = pipe_eof(from);
			if(ERROR_CODE(int) == eof_rc) 
				ERROR_RETURN_LOG(int, "Cannot check if the from segment contains data");
			if(eof_rc) ERROR_RETURN_LOG(int, "Incomplete header data");
		}

		if(to != ERROR_CODE(pipe_t))
		{
			const char* data_begin = buf;
			while(bytes_read > 0)
			{
				size_t bytes_written = pipe_hdr_write(to, data_begin, bytes_read);
				if(ERROR_CODE(size_t) == bytes_written)
					ERROR_RETURN_LOG(int, "Cannot write data to header");
				data_begin += bytes_written;
				bytes_read -= bytes_written;
				bytes_to_copy -= bytes_written;
			}
		}
		else bytes_to_copy -= bytes_read;
	}

	return 1;
}

static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	int eof_rc = pipe_eof(ctx->base);
	if(ERROR_CODE(int) == eof_rc) 
		ERROR_RETURN_LOG(int, "Cannot check if the pipe contains data");
	if(eof_rc) return 0;
	
	uint32_t i, last_written = 0;
	for(i = 0; i <= ctx->count; i ++)
	{
		pipe_t pipe = ERROR_CODE(pipe_t);
		uint32_t begin, end;
		if(i < ctx->count)
		{
			const modification_t* mod = ctx->modifications + i;
			pipe  = mod->input;
			begin = mod->offset;
			end   = end + mod->size;
		}
		else
			end = begin = ctx->base_size;

		/* Step 1: copy data within segment [last_written, begin) */
		if(begin < last_written)
		{
			int rc = _copy_header(ctx->base, ctx->output, begin - last_written);
			if(ERROR_CODE(int) == rc) ERROR_RETURN_LOG(int, "Cannot copy header within [%u, %u)", last_written, begin);
			if(rc == 0) ERROR_RETURN_LOG(int, "Incomplete header from base pipe");
		}

		/* Step 2: copy data from the modification pipe */
		if(begin < end)
		{
			int rc = _copy_header(pipe, ctx->output, end - begin);
			if(ERROR_CODE(int) == rc) ERROR_RETURN_LOG(int, "Cannot copy header from modification pipe");
			if(rc == 0) 
			{
				/* Which means we have an empty input, so copy from the base directly */
				if(1 != _copy_header(ctx->base, ctx->output, end - begin))
					ERROR_RETURN_LOG(int, "Cannot copy the header within [%u, %u)", begin ,end);
			}
			else
			{
				if(1 != _copy_header(ctx->base, ERROR_CODE(pipe_t), end - begin))
					ERROR_RETURN_LOG(int, "Cannot skip the header within [%u, %u)", begin, end);
			}
		}

		last_written = end;
	}

	return 0;
}

SERVLET_DEF = {
	.desc = "The servlet used to modify fields on the fly",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _cleanup,
	.exec = _exec
};
