/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <string.h>
#include <errno.h>

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
	uint32_t     raw_input:1;      /*!< If this servlet consumes raw input */
	uint32_t     inverse_match:1;  /*!< Inverse matching, let all string that doesn't match pass */
	uint32_t     full_match:1;     /*!< Performe full-line match */
	uint32_t     simple_mode:1;    /*!< Instead of regex, using simple KMP algorithm for matching */
	char         eol_marker;       /*!< The end-of-line marker, by default is \n */
	uint32_t     line_buf_size;    /*!< The maximum size of the line buffer */

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

	/********** Thread locals ***************/
	pstd_thread_local_t*  thread_buffer;   /*!< The thread local data buffer */
} ctx_t;

/**
 * @brief The data structure for a reusable thread local text buffer
 **/
typedef struct {
	uint32_t capacity;    /*!< The capacity of the buffer */
	char*    data;        /*!< The actual data buffer address */
} text_buffer_t;

static void* _text_buffer_alloc(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;
	text_buffer_t* ret = (text_buffer_t*)malloc(sizeof(text_buffer_t));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the thread local text buffer");

	ret->capacity = 4096;
	if((ret->data = (char*)malloc(ret->capacity)) == NULL)
	{
		free(ret);
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate meory for the text buffer memory for thread");
	}

	return ret;
}

static int _text_buffer_free(void* mem, const void* data)
{
	(void)data;
	text_buffer_t* buf = (text_buffer_t*)mem;

	if(NULL != buf)
	{
		if(NULL != buf->data) free(buf->data);
		free(buf);
	}

	return 0;
}

/**
 * @brief Get the a memory buffer larger than the required size from the text buffer
 * @param buf The text buffer
 * @param required_size How many bytes we want to ensure
 * @param preserve_data Indicates if we want to preserve the data in the preivous memory buffer if we
 *                      need to resize them
 * @param ctx The servlet context
 * @return The memory buffer or NULL on error cases
 **/
static void* _get_line_buffer(text_buffer_t* buf, uint32_t required_size, int preserve_data, ctx_t* ctx)
{
	if(required_size > buf->capacity)
	{
		uint32_t new_size = buf->capacity;

		for(;new_size < required_size; new_size <<= 1);

		if(new_size > ctx->line_buf_size)
			new_size = ctx->line_buf_size;

		if(required_size > ctx->line_buf_size)
		{
			LOG_WARNING("The size of current line is larger than the maximum size allowed, stopping");
			return NULL;
		}

		if(!preserve_data)
		{
			char* new_buf = (char*)malloc(new_size);

			if(NULL == new_buf)
				ERROR_PTR_RETURN_LOG("Cannot allocate new buffer");

			free(buf->data);

			buf->data = new_buf;
		}
		else
		{
			char* new_buf = (char*)realloc(buf->data, new_size);
			if(NULL == new_buf)
				ERROR_PTR_RETURN_LOG("Cannot resize the existing buffer");

			buf->data = new_buf;
		}

		buf->capacity = new_size;
	}

	return buf->data;
}

/**
 * @brief Convert the escape sequence string to the actual char it stands for
 * @param text The text to convert
 * @param out The output char
 * @return stauts code
 **/
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

/**
 * @brief The callback function which handles the servlet init string options
 * @param data The option data
 * @return status
 **/
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
		case 'L':
			if(data.param_array[0].intval < 0 || data.param_array[0].intval >= (1ll<<22))
				ERROR_RETURN_LOG(int, "Invalid line buffer size");
			ctx->line_buf_size = (uint32_t)data.param_array[0].intval * 1024;
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
	ctx->line_buf_size = 4096 * 1024;
	ctx->thread_buffer = NULL;

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
			.description = "Simple mode, do simple string match with KMP algorithm",
			.handler     = _option_callback,
			.args        = NULL
		},
		{
			.long_opt    = "max-line-size",
			.short_opt   = 'L',
			.pattern     = "I",
			.description = "Set the maximum line buffer size in kilobytes (Default: 4096k)",
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

	if(NULL == (ctx->model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create new type model");

	if(ctx->raw_input)
	{
		/* In this case, we just need to do the shadow */
		if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("output", PIPE_MAKE_SHADOW(ctx->input) | PIPE_DISABLED, NULL)))
			ERROR_RETURN_LOG(int, "Cannot define the output pipe");
	}
	else
	{
		/* Otherwise we use the reular pipe */
		if(ERROR_CODE(pipe_t) == (ctx->output = pipe_define("output", PIPE_OUTPUT, "plumber/std/request_local/String")))
			ERROR_RETURN_LOG(int, "Cannot deine the output pipe");

		if(ERROR_CODE(pstd_type_accessor_t) == (ctx->in_tok = pstd_type_model_get_accessor(ctx->model, ctx->input, "token")))
			ERROR_RETURN_LOG(int, "Cannot get the type accessor for input.token");

		if(ERROR_CODE(pstd_type_accessor_t) == (ctx->out_tok = pstd_type_model_get_accessor(ctx->model, ctx->output, "token")))
			ERROR_RETURN_LOG(int, "Cannot get the type accessor for output.token");
	}


	if(ctx->simple_mode && NULL == (ctx->kmp = kmp_pattern_new(argv[next_opt], strlen(argv[next_opt]))))
		ERROR_RETURN_LOG(int, "Cannot compile KMP pattern");

	if(!ctx->simple_mode && NULL == (ctx->regex = re_new(argv[next_opt])))
		ERROR_RETURN_LOG(int, "Cannot compile the regular expression");

	if(!ctx->simple_mode && NULL == (ctx->thread_buffer = pstd_thread_local_new(_text_buffer_alloc, _text_buffer_free, ctx)))
		ERROR_RETURN_LOG(int, "Cannot create new thread local");

	/* For the string input, we do not need to think about the concept of line */
	if(!ctx->raw_input) ctx->eol_marker = '\0';

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

	if(NULL != ctx->thread_buffer && ERROR_CODE(int) == pstd_thread_local_free(ctx->thread_buffer))
		rc = ERROR_CODE(int);

	return rc;
}

/**
 * @brief Read the next block of data from the input
 * @note This function handles multiple cases:
 *       (a) Read from a raw pipe, which supports Direct Buffer Access: We are able to return a buffer contains data, but
 *           the module probably not sure on the length of the data
 *       (b) Read from a raw pipe, which only supports standard pipe_read call: We just use the local buffer and the number
 *           of the bytes in the line is determined by the IO module
 *       (c) A string pipe, which we are able to return a char* contains all data
 * @param ctx The servlet context
 * @param ti  The type instance
 * @param local_buf The local buffer, which will be used for pipe_read
 * @param local_buf_size The size of the local buffer
 * @param result The buffer used to pass the result pointer out
 * @param max_size The buffer used to return the *possible* maximum size. For case (a) the size is undetermined and this
 *        is the upper bound of the size
 * @param determined_size The buffer used to return if the size has been determined
 * @return A integer indicates if the pipe contains more data, error code for error cases
 **/
static inline int _read_next_buffer(ctx_t* ctx, pstd_type_instance_t* ti, char* local_buf, size_t local_buf_size, const char** result, size_t* max_size, int* determined_size)
{
	if(ctx->raw_input)
	{
		*determined_size = 1;
		/* If this is a raw input, do the actual IO */
		size_t min_size_buf;
		int rc;
		if(ERROR_CODE(int) == (rc = pipe_data_get_buf(ctx->input, (size_t)-1, (void const**)result, &min_size_buf, max_size)))
			ERROR_RETURN_LOG(int, "The direct buffer access returns an error");

		if(rc == 0)
		{
			/* Note: This actually assumes the pipe_read call never beyond the line range, so HTTP request might not fit the assumption
			 * and we don't need to provide EOM feedback to module as well
			 **/
			size_t read_rc = pipe_read(ctx->input, local_buf, local_buf_size);

			*max_size = read_rc;
			*result = local_buf;

			if(read_rc == 0)
			{
				int eof_rc = pipe_eof(ctx->input);

				if(eof_rc == ERROR_CODE(int))
					ERROR_RETURN_LOG(int, "Cannot check if the input pipe reached the end");

				if(eof_rc == 1) return 0;
			}
		}
		else *determined_size = (min_size_buf == *max_size);

		return 1;
	}

	*determined_size = 0;

	scope_token_t tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, ti, ctx->in_tok);
	if(ERROR_CODE(scope_token_t) == tok)
		ERROR_RETURN_LOG(int, "Cannot read the string header");

	const pstd_string_t* str = pstd_string_from_rls(tok);

	if(NULL == str)
		ERROR_RETURN_LOG(int, "Cannot get the RLS string object from RLS");

	if(NULL == (*result = pstd_string_value(str)))
		ERROR_RETURN_LOG(int, "Cannot get the string buffer from the RLS string object");

	if(ERROR_CODE(size_t) == (*max_size = pstd_string_length(str)))
		ERROR_RETURN_LOG(int, "Cannot get the maximum size of the string");

	return 0;
}

static int _exec(void* ctxmem)
{
	int rc = ERROR_CODE(int);

	ctx_t* ctx = (ctx_t*)ctxmem;

	pstd_type_instance_t* ti = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->model);

	if(NULL == ti) ERROR_RETURN_LOG(int, "Cannot create new type instance");

	char local_buf[4096];
	size_t kmp_state = 0;
	int has_more_data = 1;

	const char* line_buffer = NULL;
	char* thread_buffer = NULL;
	size_t line_size = 0;
	int needs_release_buffer = 0;

	enum {
		UNMATCHED = 0,
		MATCHED   = 1,
		UNKNOWN   = 2
	} matched = UNKNOWN;

	text_buffer_t* tb = NULL;

	if(!ctx->simple_mode && NULL == (tb = pstd_thread_local_get(ctx->thread_buffer)))
		ERROR_LOG_GOTO(RET, "Cannot get the thead local buffer");

	for(;has_more_data;)
	{
		const char* buffer;   /* The buffer contains data to process */
		size_t total_size;    /* The total size of the buffer */
		has_more_data = _read_next_buffer(ctx, ti, local_buf, sizeof(local_buf), &buffer, &total_size, &needs_release_buffer);
		if(ERROR_CODE(int) == has_more_data) goto RET;

		size_t used_size = 0;

		if(ctx->simple_mode)
		{
			if(matched == UNKNOWN)
			{
				if(ctx->full_match)
				{
					size_t new_state = kmp_full_match(ctx->kmp, buffer, ctx->eol_marker, kmp_state, total_size);

					if(ERROR_CODE(size_t) == new_state)
						ERROR_LOG_GOTO(RET, "Cannot match the next text buffer");

					if(new_state == 0)
					{
						matched = UNMATCHED;
						used_size = 0;
						goto SKIP_LINE;
					}
					else
					{
						used_size = new_state - kmp_state;
						/* Strip the EOL marker as well */
						if(total_size < used_size)
						{
							used_size ++;
							has_more_data = 0;
						}
						kmp_state = new_state;
					}

				}
				else
				{
					size_t match_result = kmp_partial_match(ctx->kmp, buffer, total_size, ctx->eol_marker, &kmp_state);

					if(ERROR_CODE(size_t) == match_result)
						ERROR_LOG_GOTO(RET, "Cannot do KMP partial match");

					if(match_result < total_size && buffer[match_result] != ctx->eol_marker)
					{
						matched = MATCHED;

						used_size = match_result + kmp_pattern_length(ctx->kmp);

						goto SKIP_LINE;
					}
					else if(match_result < total_size && buffer[match_result] == ctx->eol_marker)
					{
						used_size = match_result + 1;
						has_more_data = 0;
					}
					else
						used_size = total_size;
				}
			}
			else
			{
SKIP_LINE:
				for(;used_size < total_size && buffer[used_size] != ctx->eol_marker; used_size ++);

				/* Finally we need to strip the EOL marker as well */
				if(used_size < total_size)
				{
					used_size ++;
					has_more_data = 0;
				}
			}
		}
		else
		{
			if(line_buffer == NULL)
			{
				for(used_size = 0; used_size < total_size && buffer[used_size] != ctx->eol_marker; used_size ++);

				if(used_size < total_size)
				{
					/* If we have a EOL marker  in buffer, it means we can use the returned buffer */
					line_size = used_size;
					used_size ++;
					has_more_data = 0;
					line_buffer = buffer;
				}
				else
				{
					/* We don't have meet EOL, thus we need to copy */
					if(NULL == (line_buffer  = thread_buffer = _get_line_buffer(tb, (uint32_t)total_size, 0, ctx)))
						ERROR_LOG_GOTO(RET, "Cannot get the line buffer");

					memcpy(thread_buffer, buffer, total_size);
					line_size += total_size;
				}
			}
			else
			{
				for(used_size = 0; used_size < total_size && buffer[used_size] != ctx->eol_marker; used_size ++)
				{
					line_size ++;

					if(tb->capacity < line_size && NULL == (line_buffer = thread_buffer = _get_line_buffer(tb, (uint32_t)line_size, 1, ctx)))
						ERROR_LOG_GOTO(RET, "Cannot resize the line buffer");

					thread_buffer[line_size - 1] = buffer[used_size];
				}

				if(used_size < total_size)
				{
					used_size ++;
					has_more_data = 0;
				}
			}
		}


		if(needs_release_buffer && pipe_data_release_buf(ctx->input, buffer, used_size) == ERROR_CODE(int))
			ERROR_LOG_GOTO(RET, "Cannot release the buffer");

		/* If we haven't use up the buffer yet, this indicates that we are getting the end of line */
		if(used_size < total_size) has_more_data = 0;
	}

	if(ctx->simple_mode && matched == UNKNOWN)
		matched = (kmp_state == kmp_pattern_length(ctx->kmp)) ? MATCHED : UNMATCHED;

	/* If this is not a simple mode servlet, we need to performe reular expression on the full line buffer */
	if(!ctx->simple_mode)
	{
		int match_rc;
		if(!ctx->full_match)
			match_rc = re_match_partial(ctx->regex, line_buffer, line_size);
		else
			match_rc = re_match_full(ctx->regex, line_buffer, line_size);

		if(ERROR_CODE(int) == match_rc)
			ERROR_LOG_GOTO(RET, "Cannot match the regular expression");

		matched = match_rc > 0 ? MATCHED : UNMATCHED;
	}

	if((ctx->inverse_match && matched == UNMATCHED) || (!ctx->inverse_match && matched == MATCHED))
	{
		/* Only in this case we need to produce the output */
		if(ctx->raw_input)
		{
			/* If this is the raw input, what we need is simply enable the shadow output */
			if(ERROR_CODE(int) == pipe_cntl(ctx->output, PIPE_CNTL_CLR_FLAG, PIPE_DISABLED))
				ERROR_RETURN_LOG(int, "Cannot remove the disabled flag");
		}
		else
		{
			/* Otherwise this should be a string output */
			scope_token_t tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, ti, ctx->in_tok);

			if(ERROR_CODE(scope_token_t) == tok) ERROR_RETURN_LOG(int, "Cannot read the input token");

			if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(ti, ctx->out_tok, tok))
				ERROR_RETURN_LOG(int, "Cannot write the input token");
		}
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
