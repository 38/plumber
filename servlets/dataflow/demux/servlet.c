/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <regex.h>
#include <time.h>

#include <pservlet.h>
#include <pstd.h>

#include <utils/hash/murmurhash3.h>

#define HASH_SIZE 97

/**
 * @brief The pattern hash table 
 **/
typedef struct hashnode_t {
	uint64_t hashcode[2];     /*!< The hash code */
	char*    value;           /*!< The actual value */
	struct hashnode_t* next;  /*!< The next hash node */
} hashnode_t;

typedef struct {
	regex_t regex;
	int     init;
} regex_pattern_t;

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
		regex_pattern_t*     regex;   /*!< The reuglar expression pattern table */
		void*                generic; /*!< The generic pointer */
	} pattern_table;          /*!< The pattern table */

	pstd_type_model_t*    type_model;      /*!< Type model */
	pstd_type_accessor_t  cond_acc;        /*!< The condition accessor */
} context_t;

static inline hashnode_t* _hashnode_new(const char* str, uint32_t seed)
{
	hashnode_t* ret = (hashnode_t*)malloc(sizeof(*ret));
	if(NULL == ret) 
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the hash code");
	size_t sz = strlen(str);
	murmurhash3_128(str, sz, seed, ret->hashcode);

	if(NULL == (ret->value = (char*)malloc(sz + 1)))
	{
		free(ret);
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the value");
	}

	memcpy(ret->value, str, sz);

	ret->next = NULL;
	return ret;
}
static inline uint32_t _hash_get_slot(hashnode_t* node)
{
	uint32_t multipler = 2 * (uint32_t)(0x800000000000ull % HASH_SIZE);
	return (uint32_t)((multipler * node->hashcode[0]) + node->hashcode[1]) % HASH_SIZE;  
}

static inline int _hashnode_insert(context_t* ctx, const char* str)
{
	hashnode_t* node = _hashnode_new(str, ctx->seed);
	if(NULL == node) 
		ERROR_RETURN_LOG(int, "Cannot create new node for the hash table");

	uint32_t slot = _hash_get_slot(node);

	node->next = ctx->pattern_table.string[slot];
	ctx->pattern_table.string[slot] = node;

	return 0;
}

static inline int _hashnode_free(hashnode_t* node)
{
	if(NULL != node->value) free(node->value);
	free(node);

	return 0;
}

static int _set_option(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* args)
{
	(void)nparams;
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
		    if(nparams != 1 || params[0].type != PSTD_OPTION_STRING)
		        ERROR_RETURN_LOG(int, "The field expression is expected");
		    expected_mode = MODE_NUMERIC;
		    field = params[0].strval;
SET_MODE:
		    if(ctx->mode != MODE_MATCH)
		        ERROR_RETURN_LOG(int, "Only one mode specifier can be passed");
		    ctx->mode = expected_mode;
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
			.pattern     = "S",
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
			if(NULL == (ctx->pattern_table.regex = (regex_pattern_t*)calloc(ctx->ncond, sizeof(regex_pattern_t))))
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
		if(ERROR_CODE(pipe_t) == (ctx->output[i] = pipe_define_pattern("out%u", PIPE_MAKE_SHADOW(ctx->data) | PIPE_DISABLED, "$Tdata", i)))
		    ERROR_LOG_GOTO(ERR, "Cannot define the output pipe");
		switch(ctx->mode)
		{
			case MODE_REGEX:
				if(0 == (rc = regcomp(&ctx->pattern_table.regex[i].regex, argv[opt_rc + i], 0)))
					ctx->pattern_table.regex->init = 1;
				else
				{
					char buffer[1024];
					regerror(rc, &ctx->pattern_table.regex[i].regex, buffer, sizeof(buffer));
					ERROR_LOG_GOTO(ERR, "Can't compile regex: %s", buffer);
				}
				break;
			case MODE_MATCH:
				if(ERROR_CODE(int) == _hashnode_insert(ctx, argv[opt_rc + i]))
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
		{
			for(i = 0; i < ctx->ncond; i ++)
				regfree(&ctx->pattern_table.regex[i].regex);
			free(ctx->pattern_table.regex);
		}
		else if(ctx->mode == MODE_MATCH)
		{
			for(i =0; i < HASH_SIZE; i ++)
			{
				hashnode_t* ptr, *cur;
				for(ptr = ctx->pattern_table.string[i]; NULL != ptr;)
				{
					cur = ptr;
					ptr = ptr->next;
					_hashnode_free(cur);
				}
			}
			free(ctx->pattern_table.string);
		}
	}

	return ERROR_CODE(int);
}

static inline int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	int rc = 0;

	if(NULL != ctx->output) free(ctx->output);
	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);
	
	if(ctx->pattern_table.generic != NULL)
	{
		if(ctx->mode == MODE_REGEX)
		{
			uint32_t i;
			for(i = 0; i < ctx->ncond; i ++)
				regfree(&ctx->pattern_table.regex[i].regex);
			free(ctx->pattern_table.regex);
		}
		else if(ctx->mode == MODE_MATCH)
		{
			uint32_t i;
			for(i = 0; i < HASH_SIZE; i ++)
			{
				hashnode_t *ptr, *cur;
				for(ptr = ctx->pattern_table.string[i]; NULL != ptr;)
				{
					cur = ptr;
					ptr = ptr->next;
					if(ERROR_CODE(int) == _hashnode_free(cur))
						rc = ERROR_CODE(int);
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
	.init = _init,
	.unload = _unload
};
