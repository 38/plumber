/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>
#include <proto.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	pipe_t               input;   /*!< The input pipe */
	pipe_t               output;  /*!< The output pipe */
	pstd_type_model_t*   model;   /*!< The type model */
	pstd_type_accessor_t accessor;/*!< The field accessor */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	if(argc != 2) ERROR_RETURN_LOG(int, "Usage: %s <field-to-extract>", argv[0]);

	const char* field = argv[1];
	char buf[4096];

	snprintf(buf, sizeof(buf), "$T.%s", field);

	if(ERROR_CODE(pipe_t) == (ctx->input = pipe_define("input", PIPE_INPUT, "$T")))
	    ERROR_RETURN_LOG(int, "Cannot create the input pipe");

	if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("output", PIPE_OUTPUT, buf)))
	    ERROR_RETURN_LOG(int, "Cannot create the output pipe");

	if(NULL == (ctx->model = pstd_type_model_new()))
	    ERROR_RETURN_LOG(int, "Cannot create the type model");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->accessor = pstd_type_model_get_accessor(ctx->model, ctx->input, field)))
	    ERROR_RETURN_LOG(int, "Cannot get the type accessor");

	return 0;
}

static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	size_t ti_size = pstd_type_instance_size(ctx->model);
	if(ERROR_CODE(size_t) == ti_size)
	    ERROR_RETURN_LOG(int, "Cannot get the type instance size");
	char ti_mem[ti_size];

	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->model, ti_mem);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create new type instance");

	size_t sz = pstd_type_instance_field_size(inst, ctx->accessor);
	if(ERROR_CODE(size_t) == sz) ERROR_RETURN_LOG(int, "Cannot get the size of the field");

	char buf[sz];

	size_t bytes_read = pstd_type_instance_read(inst, ctx->accessor, buf, sz);
	if(ERROR_CODE(size_t) == bytes_read)
	    ERROR_RETURN_LOG(int, "Cannot read the header");

	const char* begin = buf;

	while(bytes_read > 0)
	{
		size_t bytes_written = pipe_hdr_write(ctx->output, begin, bytes_read);
		if(ERROR_CODE(size_t) == bytes_written) ERROR_RETURN_LOG(int, "Cannot write bytes to the pipe");

		begin += bytes_written;
		bytes_read -= bytes_written;
	}

	return 0;
}

static int _cleanup(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	if(ERROR_CODE(int) == pstd_type_model_free(ctx->model))
	    ERROR_RETURN_LOG(int, "Cannot dispose the type model");

	return 0;
}

SERVLET_DEF = {
	.desc = "Extract a field from the input",
	.version = 0,
	.size = sizeof(context_t),
	.init =  _init,
	.unload = _cleanup,
	.exec = _exec
};
