/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <string.h>

#include <pservlet.h>
#include <proto.h>
#include <pstd.h>
#include <pstd/types/string.h>

#include <re.h>
#include <kmp.h>

/**
 * @brief The servlet context 
 **/
typedef struct {
	/********** Servlet Options *************/
	uint32_t     raw_input:1;      /*!< If this servlet consumes the raw input */
	uint32_t     inverse_match:1;  /*!< Inverse matching, let all string that doesn't match pass */
	uint32_t     full_match:1;     /*!< Performe full-line match */
	uint32_t     simple_mode:1;    /*!< Instead of regex, using simple KMP algorithm for matching */
	char         eol_marker;       /*!< The end-of-line marker, by default is \n */

	union {
		re_t*            regex;    /*!< The regular expression object */
		kmp_pattern_t*   kmp;      /*!< The KMP pattern */
	};
	
	/********** Pipe definitions *************/
	pipe_t       input;            /*!< The input pipe */
	pipe_t       output;           /*!< The output pipe */

	/********** Servlet Type Traits **********/
	pstd_type_accessor_t  in_tok;  /*!< The accessor to access the token of input string */
	pstd_type_accessor_t  out_tok; /*!< The accessor to access the token of output string */
	pstd_type_model_t*    model;   /*!< The type model for this servlet */
} ctx_t;

static int _escape_sequence(const char* text, char* out)
{
	if(text[0] == 0)
		return ERROR_CODE(int);
	else if(text[0] != '\\') 
		*out = text[0];
	else
	{
		char cur = text[1];
		char escape_str[] = "bfnrtv\\\'\"";
		char target_str[] = "\b\f\n\r\t\v\\\'\"";
		uint32_t i;
		for(i = 0; i < sizeof(escape_str) - 1; i ++)
			if(escape_str[i] == cur)
			{
				*out = target_str[i];
				return 0;
			}
		if((cur >= '0' && cur <= '7') || cur == 'x')
		{
			char* endp;
			const char* startp = text + 1 + (cur == 'x');
			*out = (char)strtol(startp, &endp, cur == 'x' ? 16 : 8);
			return 0;
		}
		else return ERROR_CODE(int);
	}
	return 0;
}

static int _option_callback(pstd_option_data_t data)
{
	ctx_t* ctx = (ctx_t*)data.cb_data;
	switch(data.current_option->short_opt)
	{
		case 'R':
			ctx->raw_input = 1;
			break;
		case 'I':
			ctx->inverse_match = 1;
			break;
		case 'F':
			ctx->full_match = 1;
			break;
		case 'D':
			if(ERROR_CODE(int) == _escape_sequence(data.param_array[0].strval, &ctx->eol_marker))
				ERROR_RETURN_LOG(int, "Invalid escape sequence: %s", data.param_array[0].strval);
			break;
		case 's':
			ctx->simple_mode = 1;
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid command line options");
	}
	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	ctx->raw_input = 0;
	ctx->inverse_match = 0;
	ctx->simple_mode = 0;
	ctx->eol_marker = '\n';
	ctx->full_match = 0;
	ctx->model = NULL;
	ctx->regex = NULL;

	static pstd_option_t options[] = {
		{
			.long_opt    = "help",
			.short_opt   = 'h',
			.pattern     = "",
			.description = "Show this help message",
			.handler     = pstd_option_handler_print_help,
			.args        = NULL
		},
		{
			.long_opt    = "raw-input",
			.short_opt   = 'R',
			.pattern     = "",
			.description = "Read from the untyped input pipe instead of string pipe",
			.handler     = _option_callback,
			.args        = NULL
		},
		{
			.long_opt    = "inverse",
			.short_opt   = 'I',
			.pattern     = "",
			.description = "Do inverse match, filter all the matched string out",
			.handler     = _option_callback,
			.args        = NULL
		},
		{
			.long_opt    = "full",
			.short_opt   = 'F',
			.pattern     = "",
			.description = "Turn on the full-line-matching mode",
			.handler     = _option_callback,
			.args        = NULL
		},
		{
			.long_opt    = "deliminator",
			.short_opt   = 'D',
			.pattern     = "S",
			.description = "Set the end-of-line marker",
			.handler     = _option_callback,
			.args        = NULL
		},
		{
			.long_opt    = "simple",
			.short_opt   = 's',
			.pattern     = "",
			.description = "Set the end-of-line marker",
			.handler     = _option_callback,
			.args        = NULL
		}
	};

	if(ERROR_CODE(int) == pstd_option_sort(options, sizeof(options) / sizeof(options[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the servlet option template");

	uint32_t next_opt;
	if(ERROR_CODE(uint32_t) == (next_opt = pstd_option_parse(options, sizeof(options) / sizeof(options[0]), argc, argv, ctx)))
		ERROR_RETURN_LOG(int, "Cannot parse the command line arguments");

	if(next_opt >= argc)
		ERROR_RETURN_LOG(int, "Missing regular expression");

	if(next_opt < argc - 1)
		ERROR_RETURN_LOG(int, "Too many regular expressions");

	if(ERROR_CODE(pipe_t) == (ctx->input = pipe_define("input", PIPE_INPUT, ctx->raw_input ? NULL : "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot define the input pipe");

	if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("output", PIPE_OUTPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot define the output pipe");

	if(NULL == (ctx->model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create new type model");

	if(ctx->raw_input == 0 && ERROR_CODE(pstd_type_accessor_t) == (ctx->in_tok = pstd_type_model_get_accessor(ctx->model, ctx->input, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for input.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_tok = pstd_type_model_get_accessor(ctx->model, ctx->output, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for output.token");

	if(ctx->simple_mode)
	{
		if(NULL == (ctx->kmp = kmp_pattern_new(argv[next_opt], strlen(argv[next_opt]))))
			ERROR_RETURN_LOG(int, "Cannot compile KMP pattern");
	}
	else if(NULL == (ctx->regex = re_new(argv[next_opt])))
		ERROR_RETURN_LOG(int, "Cannot compile the regular expression");

	LOG_DEBUG("Regex servlet has been initialized successfully");

	return 0;
}

static int _cleanup(void* ctxmem)
{
	int rc = 0;
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(NULL != ctx->model && ERROR_CODE(int) == pstd_type_model_free(ctx->model))
		rc = ERROR_CODE(int);

	if(!ctx->simple_mode)
	{
		if(NULL != ctx->regex && ERROR_CODE(int) == re_free(ctx->regex))
			rc = ERROR_CODE(int);
	}
	else
	{
		if(NULL != ctx->kmp && ERROR_CODE(int) == kmp_pattern_free(ctx->kmp))
			rc = ERROR_CODE(int);
	}

	return rc;
}

static int _exec(void* ctxmem)
{
	int rc = ERROR_CODE(int);

	ctx_t* ctx = (ctx_t*)ctxmem;

	pstd_type_instance_t* ti = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->model);

	if(NULL == ti) ERROR_RETURN_LOG(int, "Cannot create new type instance");

	for(;;)
	{
		char _local_buf[4096];
		
		const char* buffer;
		size_t max_buffer_size;
		int has_more = 1;

		if(ctx->raw_input)
		{
			size_t max_size;
			size_t min_size;
			int brc;
			if(ERROR_CODE(int) == (brc = pipe_data_get_buf(ctx->input, sizeof(_local_buf), (const void**)&buffer, &min_size, &max_size)))
				ERROR_LOG_GOTO(RET, "Direct buffer access function returns an error");

			if(brc == 0)
			{
				size_t read_rc = pipe_read(ctx->input, _local_buf, sizeof(_local_buf));

				if(read_rc == 0) 
				{
					int eof_rc = pipe_eof(ctx->input);

					if(eof_rc == ERROR_CODE(int))
						ERROR_LOG_GOTO(RET, "Cannot check if the input stream reached end");

					if(eof_rc == 1) 
					{
						has_more = 0;
						break;
					}
				}

				max_buffer_size = read_rc;
				has_more = 1;
				buffer = _local_buf;
			}
			else
			{
				max_buffer_size = max_size;
				has_more = 1;
			}
		}
		else
		{
			scope_token_t tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, ti, ctx->in_tok);
			if(ERROR_CODE(scope_token_t) == tok) 
				ERROR_LOG_GOTO(RET, "Cannot access the RLS token");

			const pstd_string_t* str = pstd_string_from_rls(tok);

			if(NULL == str)
				ERROR_LOG_GOTO(RET, "Cannot get the string object from the RLS");

			if(NULL == (buffer = pstd_string_value(str)))
				ERROR_LOG_GOTO(RET, "Cannot get the string value of the RLS");

			if(ERROR_CODE(size_t) == (max_buffer_size = pstd_string_length(str)))
				ERROR_LOG_GOTO(RET, "Cannot get the length of the string");
		}

		size_t used_size = 0;

		/* TODO: match the content inside the buffer and mantain used size */

		if(_local_buf != buffer  && ctx->raw_input && pipe_data_release_buf(ctx->input, buffer, used_size) == ERROR_CODE(int))
			ERROR_LOG_GOTO(RET, "Cannot release the buffer");

		if(!has_more) break;
	}


	rc = 0;
RET:
	if(ERROR_CODE(int) == pstd_type_instance_free(ti))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	return rc;
}

SERVLET_DEF = {
	.desc    = "The regular expression filter servelt",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _cleanup,
	.exec    = _exec
};
