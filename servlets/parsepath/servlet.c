/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <pservlet.h>
#include <pstd.h>
#include <proto.h>

#include <pstd/types/string.h>

typedef struct {
	uint32_t prefix_level;   /*!< How many level of directory are considered to be prefix */
	uint32_t need_extname:1; /*!< Do we need the extension name? */
	pipe_t origin;           /*!< The original input pipe */
	pipe_t prefix;           /*!< The pipe outputs the prefix */
	pipe_t relative;         /*!< The relative path */
	pipe_t error;            /*!< The error pipe */
	pipe_t extname;          /*!< The extension name of the path, only valid when the extension name mode is open */

	uint32_t token_ofs;      /*!< The offest of the token */
	uint32_t token_sz;       /*!< The size of the token */
} context_t;

/**
 * @brief Setup the error pipe
 * @param ctx the context 
 * @return status code
 **/
static inline int _set_error(const context_t* ctx)
{
	char ch = 0;
	size_t write_rc;
	while(1 != (write_rc = pipe_write(ctx->error, &ch, 1)))
		if(ERROR_CODE(size_t) == write_rc)
			return ERROR_CODE(int);
	return 0;
}

static inline int _write_string_range(pstd_string_t* str, const char* begin, size_t count)
{
	size_t rc = pstd_string_write(str, begin, count);
	if(ERROR_CODE(size_t) == rc)
		ERROR_RETURN_LOG(int, "Cannot write data to string");

	return 0;
}

static inline int _write_token(pipe_t target, size_t hbegin, size_t hend, pstd_string_t* str)
{
	 
	 scope_token_t token;

	 if(ERROR_CODE(scope_token_t) == (token = pstd_string_commit(str)))
	 {
		 pstd_string_free(str);
		 ERROR_RETURN_LOG(int, "Cannot commit string object to RLS");
	 }

	 char buf[hend];
	 memset(buf, 0, hend);
	 *(scope_token_t*)(buf + hbegin) = token;

	 size_t bytes_to_write = hend;
	 char* buf_ptr = buf;
	 while(bytes_to_write > 0)
	 {
		 size_t rc = pipe_hdr_write(target, buf_ptr, bytes_to_write);
		 if(ERROR_CODE(size_t) == rc)
			 ERROR_RETURN_LOG(int, "Cannot write header to the pipe");
		 
		 bytes_to_write -= rc;
		 buf_ptr += rc;
	 }

	 return 0;
}
/**
 * @brief Write path to the target pipe
 * @param target the target pipe
 * @param begin  the begin pointer
 * @param end    the end pointer
 * @param n      the number of segments
 * @return status code
 **/
static inline int _write_path(pipe_t target, size_t hbegin, size_t hend, char const* const* begin, char const* const* end, uint32_t n)
{
	pstd_string_t* result = pstd_string_new(128);
	if(NULL == result)
		ERROR_RETURN_LOG(int, "Cannot create new string object for the path");

	const char sep = '/';

	 if(ERROR_CODE(int) == _write_string_range(result, &sep, 1))
		ERROR_RETURN_LOG(int, "Cannot write bytes to range");

	 uint32_t i;
	 for(i = 0; i < n; i ++)
	 {
		 if(i > 0 && ERROR_CODE(int) == _write_string_range(result, &sep, 1))
			 ERROR_RETURN_LOG(int, "Cannot write seperator");

		 if(ERROR_CODE(int) == _write_string_range(result, begin[i], (size_t)(end[i] - begin[i])))
			 ERROR_RETURN_LOG(int, "Cannot write the segment to string");
	 }

	 return _write_token(target, hbegin, hend, result);
}

/**
 * @brief Simpify the path, for example a/b/c/../d ==&gt; a/b/d etc <br/>
 *        If the path is out of current root, return an error case
 * @param path The path to simpify
 * @param ctx The servlet context
 * @return the result stirng, NULL on error case
 **/
static inline int _simplify_path(const char* path, const context_t* ctx)
{
	/* The reason why we only have PATH_MAX / 2 segments is because we define the segment as
	 * *non-empty* string seperated by slash '/'. Which means we *must* have at least one char
	 * otehr than '/'. Thus, if the maximum length of path allowed is PATH_MAX, the maximum number
	 * of segment is less than PATH_MAX / 2 */
	const char *bs[PATH_MAX / 2 + 1], *es[PATH_MAX / 2 + 1];
	const char *begin = path, *end = path;
	const char *extname = NULL;
	
	int32_t sp = 0;
	for(;sp >= 0;end++)
	    if(*end == '/' || *end == 0)
	    {
		    if(begin < end)
		    {
				/* If there's an non-empty segment */
			    if(end - begin == 2 && begin[0] == '.' && begin[1] == '.') 
					sp --;
			    else if(end - begin > 1 || begin[0] != '.') 
					bs[sp] = begin, es[sp] = end, sp ++;
		    }
			/* Then let's move on to the next segment, which sould start with the next char after current one */
		    begin = end + 1;
		    if(*end == 0) break;
			extname = NULL;
	    }
		else if(ctx->need_extname && *end == '.')
			extname = end + 1;

	/* If we pop the stack too much, this should not be allowed */
	if(sp < 0) return _set_error(ctx);

	uint32_t start = 0;

	if(ctx->prefix_level > 0 && ctx->prefix_level <= (uint32_t)sp)
	{
		if(ERROR_CODE(int) == _write_path(ctx->prefix, ctx->token_ofs, ctx->token_ofs + ctx->token_sz, bs, es, ctx->prefix_level))
			ERROR_RETURN_LOG(int, "Cannot write the path to pipe");

		start += ctx->prefix_level;
	}

	if(ERROR_CODE(int) == _write_path(ctx->relative, ctx->token_ofs, ctx->token_ofs + ctx->token_sz, bs, es, (uint32_t)(sp) - ctx->prefix_level))
		ERROR_RETURN_LOG(int, "Cannot write the relative path to pipe");

	if(NULL != extname)
	{
		pstd_string_t* buf = pstd_string_new(16);
		size_t len = strlen(extname);

		while(len > 0)
		{
			size_t rc = pstd_string_write(buf, extname, len);
			if(ERROR_CODE(size_t) == rc)
				ERROR_RETURN_LOG(int, "Cannot write the extension name to string object");

			len -= rc;
			extname += rc;
		}

		if(ERROR_CODE(int) == _write_token(ctx->extname, ctx->token_ofs, ctx->token_ofs + ctx->token_sz, buf))
			ERROR_RETURN_LOG(int, "Cannot write the extension name to the pipe");
	}

	return 0;
}

static int _set_options(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* args)
{
	(void)nparams;
	(void)n;
	char what = options[idx].short_opt;
	context_t* ctx = (context_t*)args;

	switch(what)
	{
		case 'I':
			if(params[0].intval < 0 || params[0].intval > PATH_MAX / 2 + 1)
				ERROR_RETURN_LOG(int, "Invalid prefix level %"PRId64, params[0].intval);
			ctx->prefix_level = (uint32_t)params[0].intval;
			return 0;
		case 'e':
			ctx->need_extname = 1;
			return 0;
		default:
			ERROR_RETURN_LOG(int, "Invalid option %c", what);
	}
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	ctx->prefix_level = 0;
	ctx->need_extname = 0;

	static pstd_option_t opts[] = {
		{
			.long_opt     = "prefix-level",
			.short_opt    = 'L',
			.description  = "How many levels of directories are considered prefix",
			.pattern      = "I",
			.handler      = _set_options
		},
		{
			.long_opt     = "ext-name",
			.short_opt    = 'e',
			.description  = "Produce the extension name to the extension name pipe",
			.pattern      = "",
			.handler      = _set_options 
		},
		{
			.long_opt     = "help",
			.short_opt    = 'h',
			.description  = "Print this help message",
			.pattern      = "",
			.handler      = pstd_option_handler_print_help
		}
	};

	uint32_t opt_rc = pstd_option_parse(opts, sizeof(opts) / sizeof(opts[0]), argc, argv, ctx);

	static const char* strtype = "plumber/std/request_local/String";

	if(opt_rc !=  argc)
		ERROR_RETURN_LOG(int, "Invalid servlet initialization string, use --help for the usage of the servlet");

	if(ERROR_CODE(pipe_t) == (ctx->origin = pipe_define("origin", PIPE_INPUT, strtype)))
		ERROR_RETURN_LOG(int, "Cannot define the input pipe for original path");

	if(ERROR_CODE(pipe_t) == (ctx->prefix = pipe_define("prefix", PIPE_OUTPUT, strtype)))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe for the prefix");

	if(ERROR_CODE(pipe_t) == (ctx->relative = pipe_define("relative", PIPE_OUTPUT, strtype)))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe for the relative path");

	if(ERROR_CODE(pipe_t) == (ctx->error = pipe_define("error", PIPE_OUTPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe for the error pipe");

	if(ctx->need_extname && ERROR_CODE(pipe_t) == (ctx->extname = pipe_define("extname", PIPE_OUTPUT, strtype)))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe for the externsion name");

	int ret = ERROR_CODE(int);

	if(ERROR_CODE(int) == proto_init())
		ERROR_RETURN_LOG(int, "Cannot initialize libproto");

	if(ERROR_CODE(uint32_t) == (ctx->token_ofs = proto_db_type_offset(strtype, "token", &ctx->token_sz)))
		ERROR_LOG_GOTO(RET, "Cannot request the field information about %s.token", strtype);

	ret = 0;
RET:
	if(ERROR_CODE(int) == proto_finalize())
		ERROR_RETURN_LOG(int, "Cannot finalize libproto");

	return ret;
}

int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	char buf[ctx->token_ofs + ctx->token_sz];
	size_t to_read = ctx->token_ofs + ctx->token_sz;
	char* start = buf;
	while(to_read > 0)
	{
		size_t rc = pipe_hdr_read(ctx->origin, buf, ctx->token_sz + ctx->token_ofs);
		if(ERROR_CODE(size_t) == rc) 
		{
			_set_error(ctx);
			ERROR_RETURN_LOG(int, "Cannot read header");
		}

		to_read -= rc;
		start += rc;
	}

	scope_token_t token = *(scope_token_t*)(buf + ctx->token_ofs);
	const pstd_string_t* input = pstd_string_from_rls(token);
	if(NULL == input)
	{
		_set_error(ctx);
		ERROR_RETURN_LOG(int, "Cannot create token");
	}

	int rc = _simplify_path(pstd_string_value(input), ctx);
	if(ERROR_CODE(int) == rc)
		_set_error(ctx);

	return 0;
}

SERVLET_DEF = {
	.desc = "Parse a HTTP path from the input pipe and output the prefix and relative path as string."
		    "If the path is invalid, the error pipe will be written with some dummy bytes to active the error handling actions.",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = NULL,
	.unload = NULL
};
