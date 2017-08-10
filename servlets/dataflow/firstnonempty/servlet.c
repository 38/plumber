/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <pstd.h>
#include <proto.h>

typedef struct {
	uint32_t      n;
	uint32_t      header_size;
	pipe_array_t* inputs;
	pipe_t        output;
} context_t;

static int _on_type_determined(pipe_t pipe, const char* type, void* ctxbuf)
{
	(void)pipe;
	int rc = 0;
	context_t* ctx = (context_t*)ctxbuf;

	if(ERROR_CODE(int) == proto_init())
	    ERROR_RETURN_LOG(int, "Cannot initialize libproto");

	if(ERROR_CODE(uint32_t) == (ctx->header_size = proto_db_type_size(type)))
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot get the size of the header type");
	}

	if(ERROR_CODE(int) == (rc = proto_finalize()))
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot fianlize the libproto");
	}

	return rc;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	if(argc != 2)
	    ERROR_RETURN_LOG(int, "Usage: %s <number-of-inputs>", argv[0]);
	ctx->header_size = 0;

	ctx->n = (uint32_t)atoi(argv[1]);

	if(NULL == (ctx->inputs = pipe_array_new("in#", PIPE_INPUT, "$T", 0, (int)ctx->n)))
	    ERROR_RETURN_LOG(int, "Cannot create the input pipes");

	if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("out", PIPE_OUTPUT, "$T")))
	{
		pipe_array_free(ctx->inputs);
		ERROR_RETURN_LOG(int, "Cannot create the output pipes");
	}

	if(ERROR_CODE(int) == pipe_set_type_callback(ctx->output, _on_type_determined, ctx))
	{
		pipe_array_free(ctx->inputs);
		ERROR_RETURN_LOG(int, "Cannot setup the type callback");
	}

	return 0;
}

static int _copy_header(const context_t* ctx, pipe_t pipe)
{
	char hdrbuf[ctx->header_size];

	uint32_t rem = ctx->header_size;
	while(rem > 0)
	{
		size_t rc = pipe_hdr_read(pipe, hdrbuf + ctx->header_size - rem, rem);
		if(ERROR_CODE(size_t) == rc)
		    ERROR_RETURN_LOG(int, "Cannot read the typed header");
		rem -= (uint32_t)rc;
	}

	rem = ctx->header_size;
	while(rem > 0)
	{
		size_t rc = pipe_hdr_write(ctx->output, hdrbuf + ctx->header_size - rem, rem);
		if(ERROR_CODE(size_t) == rc)
		    ERROR_RETURN_LOG(int, "Cannot write the typed header");
		rem -= (uint32_t)rc;
	}

	return 0;
}

static int _copy_body(const context_t* ctx, pipe_t pipe)
{
	char buf[1024];

	size_t sz;
	for(;;)
	{
		if(ERROR_CODE(size_t) == (sz = pipe_read(pipe, buf, sizeof(buf))))
		    ERROR_RETURN_LOG(int, "Cannot read bytes from header");

		if(sz == 0) break;

		const char* begin = buf;

		while(sz > 0)
		{
			size_t written;
			if(ERROR_CODE(size_t) == (written = pipe_write(ctx->output, begin, sz)))
			    ERROR_RETURN_LOG(int, "Cannot write bytes to header");

			begin += written;
			sz -= written;
		}
	}

	return 0;
}

static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	uint32_t i;
	for(i = 0; i < ctx->n; i ++)
	{
		pipe_t pipe = pipe_array_get(ctx->inputs, i);
		if(ERROR_CODE(pipe_t) == pipe)
		    ERROR_RETURN_LOG(int, "Cannot get the input pipe");

		int eof_rc = pipe_eof(pipe);
		if(eof_rc == ERROR_CODE(int))
		    ERROR_RETURN_LOG(int, "Cannot check if the input stream gets to the end");

		if(eof_rc == 0)
		{
			if(ERROR_CODE(int) != _copy_header(ctx, pipe) &&
			   ERROR_CODE(int) != _copy_body(ctx, pipe))
			    return 0;
			else
			    ERROR_RETURN_LOG(int, "Cannot copy the input to output");
		}
	}

	return 0;
}

static int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	return pipe_array_free(ctx->inputs);
}

SERVLET_DEF = {
	.desc = "Pick up the first non-empty input from N inputs",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
