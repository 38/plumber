/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>

#include <mime.h>
#include <options.h>
#include <input.h>

struct _input_ctx_t {
	const char*             root_dir;     /*!< The root directory */
	size_t                  root_dir_len; /*!< The length of the root directory */
	pipe_t                  p_input;      /*!< The input pipe */
	pstd_type_accessor_t    a_str_tok;    /*!< The accessor to the RLS token for the relative path string */
};

input_ctx_t* input_ctx_new(const options_t* options, pstd_type_model_t* type_model)
{
	input_ctx_t* ret = (input_ctx_t*)malloc(sizeof(input_ctx_t));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate the input context");

	ret->root_dir = options->root_dir;
	ret->root_dir_len = strlen(options->root_dir);

	options_input_mode_t mode = options->input_mode;
	static const char* pipe_type_map[] = {
		[OPTIONS_INPUT_MODE_RAW]            =   "plumber/base/Raw",
		[OPTIONS_INPUT_MODE_STRING]         =   "plumber/std/request_local/String",
		[OPTIONS_INPUT_MODE_STRING_FIELD]   =   "$T"
	};

	if(ERROR_CODE(pipe_t) == (ret->p_input = pipe_define("path", PIPE_INPUT, pipe_type_map[mode])))
		ERROR_LOG_GOTO(ERR, "Cannot declare the path pipe port");

	if(mode == OPTIONS_INPUT_MODE_STRING || mode == OPTIONS_INPUT_MODE_STRING_FIELD)
	{
		const char* field_expr = "token";
		char buf[1024];
		if(mode == OPTIONS_INPUT_MODE_STRING_FIELD)
		{
			snprintf(buf, sizeof(buf), "%s.token", options->path_field);
			field_expr = buf;
		}
		if(ERROR_CODE(pstd_type_accessor_t) == (ret->a_str_tok = pstd_type_model_get_accessor(type_model, ret->p_input, field_expr)))
			ERROR_LOG_GOTO(ERR, "Cannot get the accessor for the path string");
	}
	else ret->a_str_tok = ERROR_CODE(pstd_type_accessor_t);

	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}


int input_ctx_free(input_ctx_t* input_ctx)
{
	if(NULL == input_ctx)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	free(input_ctx);
	return 0;
}

size_t input_ctx_read_path(const input_ctx_t* input_ctx, pstd_type_instance_t* type_inst, char* buf, size_t buf_size, char const** extname)
{
	if(NULL == input_ctx || NULL == type_inst || NULL == buf || NULL == extname || buf_size < input_ctx->root_dir_len)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const char* path = NULL;
	
	if(input_ctx->a_str_tok != ERROR_CODE(pstd_type_accessor_t) && NULL == (path = pstd_string_get_data_from_accessor(type_inst, input_ctx->a_str_tok, NULL)))
		ERROR_RETURN_LOG(size_t, "Cannot get path string from the input");

	int inplace = 0;
	size_t used = input_ctx->root_dir_len, valid_size;
	memcpy(buf, input_ctx->root_dir, input_ctx->root_dir_len);
	if(used > 0 && buf[used - 1] == '/')
		buf[--used] = 0;

	buf[buf_size - 1] = 0;
	valid_size = used;

	if(input_ctx->a_str_tok == ERROR_CODE(pstd_type_accessor_t))
	{
		inplace = 1;
		path = buf + used;
		for(;used + 1 < buf_size;)
		{
			int eof_rc = pipe_eof(input_ctx->p_input);
			if(ERROR_CODE(int) == eof_rc)
				ERROR_RETURN_LOG(size_t, "Cannot determine if the pipe has more data");

			if(eof_rc)
			{
				buf[used] = 0;
				break;
			}

			size_t bytes_read = pipe_read(input_ctx->p_input, buf + used, buf_size - used - 1);

			if(ERROR_CODE(size_t) == bytes_read)
				ERROR_RETURN_LOG(size_t, "Cannot read bytes from the input pipe");

			used += bytes_read;
		}
	}

	/* The reason why we only have PATH_MAX / 2 segments is because we define the segment as
	 * *non-empty* string seperated by slash '/'. Which means we *must* have at least one char
	 * otehr than '/'. Thus, if the maximum length of path allowed is PATH_MAX, the maximum number
	 * of segment is less than PATH_MAX / 2 */
	const char *bs[PATH_MAX / 2 + 1], *es[PATH_MAX / 2 + 1];
	const char *begin = path + (path[0] == '/'), *end = path + (path[0] == '/');
	int32_t sp = 0, simplified = 0, i;
	const char* ext = NULL;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-overflow"
	/* Since it's unlikely that we can have sp overflowed, thus we are able to ignore that*/
	for(;sp >= 0;end++)
	    if(*end == '/' || *end == 0)
	    {
		    if(end - begin == 2 && begin[0] == '.' && begin[1] == '.')  /* Pop the stack when the segment is .. */
		        sp --, simplified = 1;
		    else if((end - begin == 1 && begin[0] == '.') || (end - begin == 0)) /* Silently swallow the segment if the segment is empty or . */
		        simplified = 1;  /* TODO: We have a flaw here, because the path / will be marked simplified but output is identical, Nothing to do with the correctness */
		    else /* Otherwise push everything to stack */
		        bs[sp] = begin, es[sp] = end, sp ++;

		    begin = end + 1;
		    if(*end == 0) break;
		    else ext = NULL;
	    }
	    else if(*end == '.') ext = end + 1;
	
	/* If we pop the stack too much, this should not be allowed, so we produce zero output */
	if(sp < 0) 
	{
		buf[0] = 0;
		return 0;
	}

	/* Since we should preserve the last slash, so we need to check this case */
	if(sp > 0 && es[sp - 1][0] == '/')
	    es[sp] = bs[sp] = "", sp ++;
#pragma GCC diagnostic pop

	if(inplace && !simplified) 
	{
		*extname = ext;
		return used;
	}

	if(sp == 0 && used + 1 < buf_size)
	{
		buf[valid_size++] = '/';
	}

	for(i = 0; i < sp && used + 1 < buf_size; i ++)
	{
		buf[valid_size++] = '/';
		size_t bytes_to_copy = (size_t)(es[i] - bs[i]);
		if(bytes_to_copy + valid_size + 1 > buf_size)
			bytes_to_copy = buf_size - valid_size - 1;
		memmove(buf + valid_size, bs[i], bytes_to_copy);
		if(bs[i] <= ext && ext < bs[i] + bytes_to_copy)
			*extname = buf + valid_size + (ext - bs[i]);
		valid_size += bytes_to_copy;
	}

	buf[valid_size] = 0;

	return valid_size;
}
