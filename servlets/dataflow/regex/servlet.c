/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <proto.h>
#include <pstd.h>

#include <re.h>

/**
 * @brief The servlet context 
 **/
typedef struct {
	/********** Servlet Options *************/
	uint32_t     raw_input:1;      /*!< If this servlet consumes the raw input */
	uint32_t     inverse_match:1;  /*!< Inverse matching, let all string that doesn't match pass */
	uint32_t     full_match:1;     /*!< Performe full-line match */
	const char*  eol_marker;       /*!< The end-of-line marker, by default is \n */
	re_t*        regex;            /*!< The regular expression object */
	
	/********** Pipe definitions *************/
	pipe_t       input;            /*!< The input pipe */
	pipe_t       output;           /*!< The output pipe */

	/********** Servlet Type Traits **********/
	pstd_type_accessor_t  in_tok;  /*!< The accessor to access the token of input string */
	pstd_type_accessor_t  out_tok; /*!< The accessor to access the token of output string */
	pstd_type_model_t*    model;   /*!< The type model for this servlet */
} ctx_t;

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
			ctx->eol_marker = data.param_array[0].strval;
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
	ctx->eol_marker = "\n";
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

	if(NULL == (ctx->regex = re_new(argv[next_opt])))
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
	
	if(NULL != ctx->regex && ERROR_CODE(int) == re_free(ctx->regex))
		rc = ERROR_CODE(int);

	return rc;
}

SERVLET_DEF = {
	.desc    = "The regular expression filter servelt",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _cleanup
};
