/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <string.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>

#include <stdlib.h>

typedef struct {
	pipe_t req;
	pipe_t user_agent;
} servlet_context_t;

int init(uint32_t argc, char const* const* argv, void* data)
{
	(void) argc;
	(void) argv;
	servlet_context_t* ctx = (servlet_context_t*)data;
	ctx->req = pipe_define("request", PIPE_INPUT, NULL);
	ctx->user_agent = pipe_define("user-agent", PIPE_OUTPUT, NULL);

	return 0;
}

int cleanup(void* data)
{
	(void) data;

	return 0;
}

int exec(void* args)
{
	servlet_context_t* ctx = (servlet_context_t*)args;
	static const char uakey[] = "User-Agent:";
	const char* ptr = uakey;
	int rc = 0, found = 0;
	pstd_bio_t* in = pstd_bio_new(ctx->req);
	pstd_bio_t* out = pstd_bio_new(ctx->user_agent);
	char ch;

#ifndef NO_SCOPE_PTR
	pstd_string_t* str = pstd_string_new(0);
#endif

	int written = 0;
	while((rc = pstd_bio_getc(in, &ch)) > 0)
	{
		written ++;
		if(found == 0)
		{
			if(ch == '\r' || ch == '\n')
			    ptr = uakey;
			else if(ptr != NULL && ch == *ptr)
			    found = !*++ptr;
			else
			    ptr = NULL;
		}
		else if(found == 1)
		{
			if(ch == '\r' || ch == '\n') found = 3;
			else
			{
#ifdef NO_SCOPE_PTR
				pstd_bio_putc(out, ch);
#else
				pstd_string_write(str, &ch, 1);
#endif
			}
		}
		else if(found > 1)
		{
			if(ch == '\r' || ch == '\n') found ++;
			else found = 2;
			if(found >= 6) break;
		}
	}

#ifndef NO_SCOPE_PTR
	scope_token_t token = pstd_string_commit(str);
	pstd_bio_write(out, &token, sizeof(token));
#endif

	if(written)
	    pipe_cntl(ctx->req, PIPE_CNTL_SET_FLAG, PIPE_PERSIST);

	pstd_bio_free(in);
	pstd_bio_free(out);
	return 0;
}

SERVLET_DEF = {
	.desc = "Request Parser",
	.version = 0,
	.size    = sizeof(servlet_context_t),
	.init    = init,
	.unload  = cleanup,
	.exec    = exec
};
