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
	pipe_t extname;          /*!< The extension name of the path, only valid when the extension name mode is open */
	pipe_t invalid;          /*!< The pipe will be written when the path is invalid */

	pstd_type_model_t*    model;          /*!< The type model we used */
	pstd_type_accessor_t  origin_token;   /*!< The origin token */
	pstd_type_accessor_t  relative_token; /*!< The relative token */
	pstd_type_accessor_t  prefix_token;   /*!< The prefix token */
	pstd_type_accessor_t  extname_token;  /*!< The externsion name token */
} context_t;

static inline int _write_string(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, pstd_string_t* str)
{
	 
	 scope_token_t token;

	 if(ERROR_CODE(scope_token_t) == (token = pstd_string_commit(str)))
	 {
		 pstd_string_free(str);
		 ERROR_RETURN_LOG(int, "Cannot commit string object to RLS");
	 }

	 return PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, accessor, token);
}
/**
 * @brief Write path to the target pipe
 * @param target the target pipe
 * @param begin  the begin pointer
 * @param end    the end pointer
 * @param n      the number of segments
 * @return status code
 **/
static inline int _write_path(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, char const* const* begin, char const* const* end, uint32_t n)
{
	pstd_string_t* result = pstd_string_new(128);
	if(NULL == result) ERROR_RETURN_LOG(int, "Cannot create new string object for the path");

	const char sep = '/';

	 if(ERROR_CODE(size_t) == pstd_string_write(result, &sep, 1)) 
		 ERROR_RETURN_LOG(int, "Cannot write bytes to range");

	 uint32_t i;
	 for(i = 0; i < n; i ++)
	 {
		 if(i > 0 && ERROR_CODE(size_t) == pstd_string_write(result, &sep, 1))
			 ERROR_RETURN_LOG(int, "Cannot write seperator");

		 if(ERROR_CODE(size_t) == pstd_string_write(result, begin[i], (size_t)(end[i] - begin[i])))
			 ERROR_RETURN_LOG(int, "Cannot write the segment to string");
	 }

	 return _write_string(inst, accessor, result);
}

/**
 * @brief Simpify the path, for example a/b/c/../d ==&gt; a/b/d etc <br/>
 *        If the path is out of current root, return an error case
 * @param path The path to simpify
 * @param ctx The servlet context
 * @return the result stirng, NULL on error case
 **/
static inline int _simplify_path(const char* path, const context_t* ctx, pstd_type_instance_t* inst)
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
	if(sp < 0) 
	{
		size_t rc;
		while(0 == (rc = pipe_write(ctx->invalid, "", 1)));
		return ERROR_CODE(size_t) != rc ? 0 : ERROR_CODE(int);
	}

	uint32_t start = 0;

	if(ctx->prefix_level > 0 && ctx->prefix_level <= (uint32_t)sp)
	{
		if(ERROR_CODE(int) == _write_path(inst, ctx->prefix_token, bs, es, ctx->prefix_level))
			ERROR_RETURN_LOG(int, "Cannot write the path to pipe");

		start += ctx->prefix_level;
	}

	if(ERROR_CODE(int) == _write_path(inst, ctx->relative_token, bs, es, (uint32_t)(sp) - ctx->prefix_level))
		ERROR_RETURN_LOG(int, "Cannot write the relative path to pipe");

	if(NULL != extname)
	{
		pstd_string_t* buf = pstd_string_new(16);
		if(ERROR_CODE(size_t) == pstd_string_write(buf, extname, strlen(extname)))
			ERROR_RETURN_LOG(int, "Cannot write extension name to string object");

		if(ERROR_CODE(int) == _write_string(inst, ctx->extname_token, buf))
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

	if(ctx->need_extname && ERROR_CODE(pipe_t) == (ctx->extname = pipe_define("extname", PIPE_OUTPUT, strtype)))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe for the externsion name");

	if(ERROR_CODE(pipe_t) == (ctx->invalid = pipe_define("invalid", PIPE_OUTPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe for the invalid bit");

	if(NULL == (ctx->model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create type model");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->prefix_token = pstd_type_get_accessor(ctx->model, ctx->prefix, "token")))
		ERROR_RETURN_LOG(int, "Cannot get accessor for field prefix.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->relative_token = pstd_type_get_accessor(ctx->model, ctx->relative, "token")))
		ERROR_RETURN_LOG(int, "Cannot get accessor for field relative.token");

	if(ctx->need_extname && ERROR_CODE(pstd_type_accessor_t) == (ctx->extname_token = pstd_type_get_accessor(ctx->model, ctx->extname, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for field extname.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->origin_token = pstd_type_get_accessor(ctx->model, ctx->origin, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accesor for field origin.token");

	return 0;
}

int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	pstd_type_instance_t* inst = NULL;
	size_t sz = pstd_type_instance_size(ctx->model);
	char buf[sz];
	
	if(ERROR_CODE(size_t) == sz) ERROR_LOG_GOTO(ERR, "Cannot get the size of the model");
	if(NULL == (inst = pstd_type_instance_new(ctx->model, buf)))
		ERROR_LOG_GOTO(ERR, "Cannot create the model");

	scope_token_t token;
	if(ERROR_CODE(scope_token_t) != (token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->origin_token)))
		ERROR_LOG_GOTO(ERR, "Cannot read the RLS token from input");
	const pstd_string_t* input = pstd_string_from_rls(token);
	if(NULL == input) ERROR_LOG_GOTO(ERR, "Cannot retrive string from the token");

	if(ERROR_CODE(int) == _simplify_path(pstd_string_value(input), ctx, inst))
		ERROR_LOG_GOTO(ERR, "Cannot simplify the path");

	return pstd_type_instance_free(inst);

ERR:
	if(NULL != inst) pstd_type_instance_free(inst);

	return ERROR_CODE(int);
}

static inline int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	return pstd_type_model_free(ctx->model);
}

SERVLET_DEF = {
	.desc = "Parse a HTTP path from the input pipe and output the prefix and relative path as string."
		    "If the path is invalid, the error pipe will be written with some dummy bytes to active the error handling actions.",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
