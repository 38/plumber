/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <pstd.h>
#include <stdio.h>
#include <string.h>

#include <pstd/types/string.h>

typedef struct {
	pipe_t user_agent;
	pipe_t response;
} servlet_context_t;


static int init(uint32_t argc, char const* const* argv, void* data)
{
	(void) argc;
	(void) argv;
	servlet_context_t* ctx = (servlet_context_t*)data;
	ctx->user_agent = pipe_define("user-agent", PIPE_INPUT, NULL);
	ctx->response   = pipe_define("response", PIPE_OUTPUT | PIPE_ASYNC, NULL);
	return 0;
}

static int cleanup(void* data)
{
	(void) data;
	return 0;
}
#define RESULT_PATTERN_PREFIX \
    "<html><head><title>Hello World</title></head>"\
    "<body>Hi there, this is Plumber!<br/>"\
     "BTW, your user agent string is "
#define RESULT_PATTERN_SUFFIX "</body></html>"

#define RESULT_PATTERN RESULT_PATTERN_PREFIX"%s"RESULT_PATTERN_SUFFIX

static const char _result_pattern[] = RESULT_PATTERN;

static int exec(void * args)
{
	(void)_result_pattern;
	servlet_context_t* ctx = (servlet_context_t*)args;

	pstd_bio_t* in = pstd_bio_new(ctx->user_agent);
#ifdef NO_SCOPE_PTR
	char ua[1024] = {};
	if((size_t)-1 == pstd_bio_read(in, ua, sizeof(ua))) return -1;
#else
	scope_token_t token;
	if(ERROR_CODE(size_t) == pstd_bio_read(in, &token, sizeof(token))) return -1;
	const pstd_string_t* uastr = pstd_string_from_rls(token);
#	if defined(NO_WRITE_SCOPE)
	const char* ua = pstd_string_value(uastr);
#	endif
#endif

	pstd_bio_t* out = pstd_bio_new(ctx->response);
	pstd_bio_puts(out, "HTTP/1.1 200 OK\r\n");
	pstd_bio_puts(out, "Content-Type: text/html\r\n");
	pstd_bio_puts(out, "Conntection: keep-alive\r\n");
	pstd_bio_printf(out, "Content-Length: %zu\r\n\r\n",
#if defined(NO_SCOPE_PTR)
	        strlen(ua)
#else
	        pstd_string_length(uastr)
#endif
	        + (sizeof(_result_pattern) - 3));
#if (defined(NO_SCOPE_PTR) || defined(NO_WRITE_SCOPE))
	pstd_bio_printf(out, RESULT_PATTERN, ua);
#else
	/* The performance penalty is really high here, we
	 * drop the performance from 115k RPS (with -DNO_SCOPE_PTR)
	 * to 11k RPS (with -DNO_WRITE_SCOPE) and then 68k RPS
	 * (without any marco).
	 * The reason for why this happen is, actually the RLS overhead is really low,
	 * however, the write_scope_token function needs to flush the BIO buffer in
	 * order to keep the data is in correct order. Then we need to make another
	 * write system call to write the token.
	 * Finally we need another system call to write the suffix, which means
	 * we need 2 more system call, and this causes the performance penalty.
	 * If we disable the sync_write_attempt in the TCP module, the performance
	 * raise back to 80k, this is reasonable, because the async write buffer
	 * can reduce the number of system calls but the async loop is another additional
	 * overhead for this simple servlet.
	 * So the overall performance ~80k is reasonable.
	 *
	 * So this is a good example of when should we use the token, it seems unless the
	 * file is large enough, we still need to write to the pipe directly.
	 **/
	pstd_bio_printf(out, RESULT_PATTERN_PREFIX);
	pstd_bio_write_scope_token(out, token);
	pstd_bio_printf(out, RESULT_PATTERN_SUFFIX);
#endif

	pstd_bio_free(in);
	pstd_bio_free(out);
	return 0;
}

SERVLET_DEF = {
	.desc = "Response Generator",
	.version = 0,
	.size    = sizeof(servlet_context_t),
	.init    = init,
	.unload  = cleanup,
	.exec    = exec
};
