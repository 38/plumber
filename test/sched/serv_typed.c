/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
typedef struct {
	pipe_t* pipes;
} context_t;

static inline int _init(uint32_t argc, char const* const* argv, void* mem)
{
	context_t* ctx = (context_t*)mem;

	if(NULL == (ctx->pipes = (pipe_t*)malloc(sizeof(pipe_t) * (argc - 2))))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the pipe array");

	uint32_t i;
	uint32_t output = 0;
	for(i = 1; i < argc; i ++)
	{
		const char* desc = argv[i];

		if(strcmp(desc, "->") == 0)
		{
			output = 1;
			continue;
		}

		static char namebuf[1024];
		static char typebuf[1024];
		const char* ptr;
		const char* type = NULL;
		size_t len = 0;
		for(ptr = desc; *ptr != 0 && *ptr != ':'; ptr ++)
			namebuf[len ++] = *ptr;
		namebuf[len] = 0;

		if(*ptr == ':')
		{
			len = 0;
			ptr ++;
			for(;*ptr != 0; ptr ++)
				typebuf[len ++] = *ptr;
			typebuf[len] = 0;
			type = typebuf;
		}

		if(ERROR_CODE(pipe_t) == (ctx->pipes[i - 1u - output] = pipe_define(namebuf, (output ? PIPE_OUTPUT : PIPE_INPUT), type)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot define pipe");
	}

	return 0;
}

static inline int _exec(void* ctx)
{
	(void)ctx;
	return 0;
}

static inline int _unload(void* mem)
{
	context_t* ctx = (context_t*)mem;

	free(ctx->pipes);

	return 0;
}
SERVLET_DEF = {
	.desc = "Typed servlet test",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
