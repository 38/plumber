/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <regex.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>

#include <utils/hash/murmurhash3.h>

#include <pservlet.h>
#include <pstd.h>

#include <pstd/types/string.h>

/**
 * @brief The size of the hash table used for the pattern
 **/
#define HASH_SIZE 31

/**
 * @brief The pattern hash table
 **/
typedef struct hashnode_t {
	uint64_t hashcode[2];     /*!< The hash code */
	pipe_t   pipe;
	struct hashnode_t* next;  /*!< The next hash node */
} hashnode_t;

/**
 * @brief The context of the servlet
 **/
typedef struct {
	enum {
		MODE_MATCH,         /*!< The mode that inputs a string as condition and do exact matching */
		MODE_REGEX,         /*!< The mode that inputs a string as condition and do regular expression match */
		MODE_NUMERIC        /*!< The mode that inputs a number from 0 to N */
	}            mode;      /*!< The mode of the dexmuer */
	const char*  field;     /*!< The field expression we want to read */

	uint32_t     seed;      /*!< The hash seed */
	uint32_t     ncond;     /*!< The number of condition we want to match */
	pipe_t       cond;      /*!< The pipe that inputs the condition input */
	pipe_t       data;      /*!< The pipe that contains the data */
	pipe_t*      output;    /*!< The pipes for the outputs */

	union {
		hashnode_t**         string;  /*!< The hash table used for string hash */
		regex_t*             regex;   /*!< The reuglar expression pattern table */
		void*                generic; /*!< The generic pointer */
	} pattern_table;          /*!< The pattern table */

	pstd_type_model_t*    type_model;      /*!< Type model */
	pstd_type_accessor_t  cond_acc;        /*!< The condition accessor */
} context_t;

/**
 * @brief Create a new hash node for the pattern
 * @param str the pattern string
 * @param pipe the pipe we could enable
 * @param seed the seed of the hash function
 * @return the newly created hash node
 **/
static inline hashnode_t* _hashnode_new(const char* str, pipe_t pipe, uint32_t seed)
{
	hashnode_t* ret = (hashnode_t*)malloc(sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the hash code");
	size_t sz = strlen(str);
	murmurhash3_128(str, sz, seed, ret->hashcode);

	ret->pipe = pipe;
	ret->next = NULL;
	return ret;
}
/**
 * @brief Compute which hash slot we should look at
 * @param hashcode The 128 bit hash code
 * @note  This is actually computes the modular of the entire 128 bit integer
 * @return The slot id
 **/
static inline uint32_t _hash_get_slot(const uint64_t* hashcode)
{
	static const uint32_t multipler = (2 * (uint32_t)((1ull << 63) % HASH_SIZE)) % HASH_SIZE;
	return (uint32_t)(multipler * hashcode[1] + hashcode[0]) % HASH_SIZE;
}

/**
 * @brief  Intert a new pattern to the pattern hash table
 * @param  ctx The servlet context
 * @param  str The pattern string
 * @param  pipe The pipe we should activated on this pattern
 * @return status code
 **/
static inline int _hashnode_insert(context_t* ctx, const char* str, pipe_t pipe)
{
	hashnode_t* node = _hashnode_new(str, pipe, ctx->seed);
	if(NULL == node)
	    ERROR_RETURN_LOG(int, "Cannot create new node for the hash table");

	uint32_t slot = _hash_get_slot(node->hashcode);

	node->next = ctx->pattern_table.string[slot];
	ctx->pattern_table.string[slot] = node;

	return 0;
}

/**
 * @brief Find the hash node which matches the pattern
 * @param ctx The servlet context
 * @param str The string to match
 * @return The hash node has been found
 **/
static inline const hashnode_t* _hash_find(const context_t* ctx, const char* str)
{
	size_t len = strlen(str);
	uint64_t hashcode[2];
	murmurhash3_128(str, len, ctx->seed, hashcode);

	uint32_t slot = _hash_get_slot(hashcode);
	const hashnode_t* ret;

	for(ret = ctx->pattern_table.string[slot];
	    NULL != ret &&
	    ret->hashcode[0] != hashcode[0] &&
	    ret->hashcode[1] != hashcode[1];
	    ret = ret->next);

	return ret;
}

/**
 * @brief Parse the servlet initialization options
 * @param idx The option index in the option definition array
 * @param params The array of the pointers
 * @param nparams How many parameters for this option
 * @param options The option definition array
 * @param n How many opitions defined in the option definition array
 * @param args The additional arguments
 * @return status code
 **/
static int _set_option(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* args)
{
	(void)n;
	char what = options[idx].short_opt;
	context_t* ctx = (context_t*)args;
	int expected_mode;
	const char* field;
	switch(what)
	{
		case 'r':
		    expected_mode = MODE_REGEX;
		    field = "token";
		    goto SET_MODE;
		case 'n':
		    if(nparams != 2 || params[0].type != PSTD_OPTION_STRING ||
		       params[1].type != PSTD_OPTION_TYPE_INT)
		        ERROR_RETURN_LOG(int, "--numeric <field_expr> <num-outputs>");
		    expected_mode = MODE_NUMERIC;
		    field = params[0].strval;
		    ctx->ncond = (uint32_t)params[1].intval;
SET_MODE:
		    if(ctx->mode != MODE_MATCH)
		        ERROR_RETURN_LOG(int, "Only one mode specifier can be passed");
		    ctx->mode = (uint32_t)expected_mode;
		    ctx->field = field;
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid option");
	}

	return 0;
}
static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	static pstd_option_t opts[] = {
		{
			.long_opt    = "regex",
			.short_opt   = 'r',
			.description = "Use the regular expression mode",
			.pattern     = "",
			.handler     = _set_option
		},
		{
			.long_opt    = "numeric",
			.short_opt   = 'n',
			.description = "Use the numeric mode",
			.pattern     = "SI",
			.handler     = _set_option
		},
		{
			.long_opt    = "help",
			.short_opt   = 'h',
			.description = "Display this help message",
			.pattern     = "",
			.handler     = pstd_option_handler_print_help
		}
	};

	ctx->mode = MODE_MATCH;
	ctx->field = "token";
	ctx->output = NULL;
	ctx->type_model = NULL;

	uint32_t opt_rc = pstd_option_parse(opts, sizeof(opts) / sizeof(opts[0]), argc, argv, ctx);

	if(ERROR_CODE(uint32_t) == opt_rc)
	    ERROR_RETURN_LOG(int, "Invalid servlet initialization string, for more information, use pstest -l %s --help", argv[0]);

	if(ctx->mode  != MODE_NUMERIC)
	    ctx->ncond = argc - opt_rc;

	if(ctx->mode == MODE_NUMERIC)
	{
		if(ERROR_CODE(pipe_t) == (ctx->cond = pipe_define("cond", PIPE_INPUT, "$Tcond")))
		    ERROR_RETURN_LOG(int, "Cannot define the condition pipe");
	}
	else if(ERROR_CODE(pipe_t) == (ctx->cond = pipe_define("cond", PIPE_INPUT, "plumber/std/request_local/String")))
	    ERROR_RETURN_LOG(int, "Cannot define the condition pipe");

	if(ERROR_CODE(pipe_t) == (ctx->data = pipe_define("data", PIPE_INPUT, "$Tdata")))
	    ERROR_RETURN_LOG(int, "Cannot define the data input pipe");

	if(NULL == (ctx->output = (pipe_t*)malloc(sizeof(ctx->output[0]) * (ctx->ncond + 1))))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the output array");

	ctx->pattern_table.generic = NULL;
	ctx->type_model = NULL;

	/* We just initalize the seed with the nanosecond number in the startup timestamp, which is quite random */
	struct timespec ts;
	if(clock_gettime(CLOCK_REALTIME, &ts) < 0)
	{
		LOG_WARNING("Cannot get the high resolution timestamp, use low resolution one instead");
		ts.tv_nsec = (long)time(NULL);
	}
	ctx->seed =  (uint32_t)ts.tv_nsec;


	switch(ctx->mode)
	{
		case MODE_REGEX:
		    if(NULL == (ctx->pattern_table.regex = (regex_t*)calloc(ctx->ncond, sizeof(ctx->pattern_table.regex[0]))))
		        ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the regular expression array");
		    break;
		case MODE_MATCH:
		    if(NULL == (ctx->pattern_table.string = (hashnode_t**)calloc(HASH_SIZE, sizeof(hashnode_t*))))
		        ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the string pattern table");
		    break;
		case MODE_NUMERIC:
		    break;
	}

	uint32_t i;
	int rc;
	for(i = 0; i < ctx->ncond; i ++)
	{
		char regbuf[1024];
		if(ERROR_CODE(pipe_t) == (ctx->output[i] = pipe_define_pattern("out%u", PIPE_MAKE_SHADOW(ctx->data) | PIPE_DISABLED, "$Tdata", i)))
		    ERROR_LOG_GOTO(ERR, "Cannot define the output pipe");
		switch(ctx->mode)
		{
			case MODE_REGEX:
			    snprintf(regbuf, sizeof(regbuf), "^%s$", argv[opt_rc + i]);
			    if(0 != (rc = regcomp(ctx->pattern_table.regex + i, regbuf, 0)))
			    {
#ifdef LOG_ERROR_ENABLED
				    char buffer[1024];
				    regerror(rc, ctx->pattern_table.regex + i, buffer, sizeof(buffer));
#endif
				    ERROR_LOG_GOTO(ERR, "Can't compile regex: %s", buffer);
			    }
			    break;
			case MODE_MATCH:
			    if(ERROR_CODE(int) == _hashnode_insert(ctx, argv[opt_rc + i], ctx->output[i]))
			        ERROR_LOG_GOTO(ERR, "Can't insert the pattern to hash table");
			    break;
			case MODE_NUMERIC:
			    break;
		}
	}

	if(ERROR_CODE(pipe_t) == (ctx->output[ctx->ncond] = pipe_define("default", PIPE_MAKE_SHADOW(ctx->data) | PIPE_DISABLED, "$Tdata")))
	    ERROR_LOG_GOTO(ERR, "Cannot define the default output pipe");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
	    ERROR_LOG_GOTO(ERR, "Cannot create type model");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->cond_acc = pstd_type_model_get_accessor(ctx->type_model, ctx->cond, ctx->field)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the accessor for the input type");

	return 0;
ERR:
	if(ctx->output != NULL) free(ctx->output);
	if(ctx->type_model != NULL) pstd_type_model_free(ctx->type_model);
	if(ctx->pattern_table.generic != NULL)
	{
		if(ctx->mode == MODE_REGEX)
		    for(i = 0; i < ctx->ncond; i ++)
		        regfree(ctx->pattern_table.regex + i);
		else if(ctx->mode == MODE_MATCH)
		{
			for(i =0; i < HASH_SIZE; i ++)
			{
				hashnode_t* ptr, *cur;
				for(ptr = ctx->pattern_table.string[i]; NULL != ptr;)
				{
					cur = ptr;
					ptr = ptr->next;
					free(cur);
				}
			}
			free(ctx->pattern_table.string);
		}
	}

	return ERROR_CODE(int);
}

static inline int _exec_match(context_t* ctx, pstd_type_instance_t* inst)
{
	scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->cond_acc);

	if(ERROR_CODE(scope_token_t) == token)
	    ERROR_RETURN_LOG(int, "Cannot read the scope token from the cond pipe");

	const pstd_string_t* ps = pstd_string_from_rls(token);
	if(NULL == ps)
	    ERROR_RETURN_LOG(int, "Cannot read string from the RLS");

	const char* str = pstd_string_value(ps);
	if(NULL == str) ERROR_RETURN_LOG(int, "Cannot get the string value");

	const hashnode_t* node = _hash_find(ctx, str);

	pipe_t picked = ctx->output[ctx->ncond];

	if(NULL != node) picked = node->pipe;

	return pipe_cntl(picked, PIPE_CNTL_CLR_FLAG, PIPE_DISABLED);
}

static inline int _exec_numeric(context_t* ctx, pstd_type_instance_t* inst)
{
	uint32_t value = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, inst, ctx->cond_acc);

	pipe_t picked = ctx->output[value >= ctx->ncond ? ctx->ncond : value];

	return pipe_cntl(picked, PIPE_CNTL_CLR_FLAG, PIPE_DISABLED);
}

static inline int _exec_regex(context_t* ctx, pstd_type_instance_t* inst)
{
	scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->cond_acc);

	if(ERROR_CODE(scope_token_t) == token)
	    ERROR_RETURN_LOG(int, "Cannot read the scope token from the cond pipe");

	const pstd_string_t* ps = pstd_string_from_rls(token);
	if(NULL == ps)
	    ERROR_RETURN_LOG(int, "Cannot read string from the RLS");

	const char* str = pstd_string_value(ps);
	if(NULL == str) ERROR_RETURN_LOG(int, "Cannot get the string value");

	pipe_t picked = ctx->output[ctx->ncond];

	uint32_t i;
	for(i = 0; i < ctx->ncond; i ++)
	{
		int rc = regexec(ctx->pattern_table.regex + i, str, 0, NULL, 0);
		if(rc == 0) break;
		else if(rc != REG_NOMATCH)
		{
#ifdef LOG_ERROR_ENABLED
			char buffer[1024];
			regerror(rc, ctx->pattern_table.regex + i, buffer, sizeof(buffer));
#endif
			ERROR_RETURN_LOG(int, "Regex error: %s", buffer);
		}
	}

	return pipe_cntl(picked, PIPE_CNTL_CLR_FLAG, PIPE_DISABLED);
}

static inline int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	int rc;
	size_t tisz = pstd_type_instance_size(ctx->type_model);
	if(ERROR_CODE(size_t) == tisz) ERROR_RETURN_LOG(int, "Cannot get the size of the type model");
	char tibuf[tisz];
	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->type_model, tibuf);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create the type instance");

	switch(ctx->mode)
	{
		case MODE_MATCH:
		    rc = _exec_match(ctx, inst);
		    break;
		case MODE_NUMERIC:
		    rc = _exec_numeric(ctx, inst);
		    break;
		case MODE_REGEX:
		    rc = _exec_regex(ctx, inst);
		    break;
		default:
		    LOG_ERROR("Invalid servlet mode");
		    rc = ERROR_CODE(int);
	}

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
	    ERROR_RETURN_LOG(int, "Cannot dipsoes the type instance");

	return rc;
}

static inline int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	int rc = 0;
	uint32_t i;

	if(NULL != ctx->output) free(ctx->output);
	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
	    rc = ERROR_CODE(int);

	if(ctx->pattern_table.generic != NULL)
	{
		if(ctx->mode == MODE_REGEX)
		    for(i = 0; i < ctx->ncond; i ++)
		        regfree(ctx->pattern_table.regex + i);
		else if(ctx->mode == MODE_MATCH)
		{
			for(i = 0; i < HASH_SIZE; i ++)
			{
				hashnode_t *ptr, *cur;
				for(ptr = ctx->pattern_table.string[i]; NULL != ptr;)
				{
					cur = ptr;
					ptr = ptr->next;
					free(cur);
				}
			}
			free(ctx->pattern_table.string);
		}
	}

	return rc;
}

SERVLET_DEF = {
	.desc = "The demultiplexer, which takes N inputs and one condition, produces the copy of selected input",
	.version = 0,
	.size = sizeof(context_t),
	.init   = _init,
	.unload = _unload,
	.exec   = _exec
};
