/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>

#include <error.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

pipe_t pipe_define(const char* name, pipe_flags_t flag, const char* type_expr)
{
	return RUNTIME_ADDRESS_TABLE_SYM->define(name, flag, type_expr);
}


size_t pipe_read(pipe_t pipe, void* result, size_t count)
{
	return RUNTIME_ADDRESS_TABLE_SYM->read(pipe, result, count);
}

size_t pipe_write(pipe_t pipe, const void* data, size_t count)
{
	return RUNTIME_ADDRESS_TABLE_SYM->write(pipe, data, count);
}

size_t pipe_hdr_read(pipe_t pipe, void* buffer, size_t nbytes)
{
	size_t ret = 0;

	int rc = pipe_cntl(pipe, PIPE_CNTL_READHDR, buffer, nbytes, &ret);
	if(ERROR_CODE(int) == rc)
	    return ERROR_CODE(size_t);

	return ret;
}

size_t pipe_hdr_write(pipe_t pipe, const void* buffer, size_t nbytes)
{
	size_t ret = 0;

	int rc = pipe_cntl(pipe, PIPE_CNTL_WRITEHDR, buffer, nbytes, &ret);

	if(ERROR_CODE(int) == rc)
	    return ERROR_CODE(size_t);

	return ret;
}

int pipe_write_scope_token(pipe_t pipe, scope_token_t token, const scope_token_data_req_t* datareq)
{
	scope_token_t internal_token = (token == ERROR_CODE(scope_token_t) || token == 0) ? ERROR_CODE(scope_token_t) : token - 1;
	return RUNTIME_ADDRESS_TABLE_SYM->write_scope_token(pipe, internal_token, datareq);
}

int pipe_eof(pipe_t pipe)
{
	return RUNTIME_ADDRESS_TABLE_SYM->eof(pipe);
}

int pipe_cntl_mod_prefix(const char* path, uint8_t* result)
{
	return RUNTIME_ADDRESS_TABLE_SYM->mod_cntl_prefix(path, result);
}

int pipe_cntl(pipe_t pipe, uint32_t opcode, ...)
{
	va_list ap;
	va_start(ap, opcode);
	int rc = RUNTIME_ADDRESS_TABLE_SYM->cntl(pipe, opcode, ap);
	va_end(ap);
	return rc;
}

pipe_t pipe_define_pattern(const char* pattern, pipe_flags_t flag, const char* type_expr, ...)
{
	char buffer[PIPE_MAX_NAME];

	va_list ap;
	va_start(ap, type_expr);
	vsnprintf(buffer, sizeof(buffer), pattern, ap);
	va_end(ap);

	return pipe_define(buffer, flag, type_expr);
}
static inline char* _append_num_to_buffer(char* b_begin, char* b_end, int serial)
{
	int n = 0;
	if(serial == 0) *(b_begin ++) = '0';
	else if(n < 0)
	{
		*(b_begin++) = '-';
		n = -serial;
	}
	else n = serial;

	char* r = b_begin, *l = b_begin;
	for(;n > 0; n /= 10)
	    if(r < b_end) *(r ++) = (char)('0' + (n % 10));
	b_begin = r;
	r --;
	for(;l < r; l++, r--)
	{
		char t = *l;
		*l = *r;
		*r = t;
	}

	return b_begin;
}
static inline const char* _render_pattern(const char* pattern, char *buffer, size_t size, int serial)
{
	int num_sign = 0;
	char* b_begin = buffer;
	char* b_end = buffer + size - 1;
	for(;*pattern && b_end - b_begin > 0; pattern ++)
	{
		char ch = *pattern;
		if(ch == '#')
		{
			if(2 == ++ num_sign)
			{
				num_sign = 0;
				if(b_end - b_begin > 0) *(b_begin++) = '#';
			}
		}
		else
		{
			if(num_sign == 1)
			{
				b_begin = _append_num_to_buffer(b_begin, b_end, serial);
				num_sign = 0;
			}

			*(b_begin ++) = ch;
		}
	}

	if(num_sign == 1) b_begin = _append_num_to_buffer(b_begin, b_end, serial);
	*b_begin = 0;

	return buffer;
}

pipe_array_t* pipe_array_new(const char* pattern, pipe_flags_t flag, const char* type_expr, int serial_begin, int serial_end)
{
	if(NULL == pattern || ERROR_CODE(pipe_flags_t) == flag) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(serial_end < serial_begin) serial_end = serial_begin;

	pipe_array_t* ret = (pipe_array_t*)malloc(sizeof(pipe_array_t) + sizeof(pipe_t) * (unsigned)(serial_end - serial_begin));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot not allocate memory for the pipe array");

	ret->size = (uint32_t)(serial_end - serial_begin);

	char buffer[PIPE_MAX_NAME];
	int i;
	for(i = serial_begin; i < serial_end; i ++)
	{
		const char* pipe_name = _render_pattern(pattern, buffer, sizeof(buffer), i);
		if(NULL == pipe_name) ERROR_LOG_GOTO(ERR, "Cannot render the pipe pattern");
		LOG_DEBUG("Create pipe %s", pipe_name);
		ret->pipes[i - serial_begin] = pipe_define(pipe_name, flag, type_expr);
	}

	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

int pipe_array_free(pipe_array_t* arr)
{
	if(NULL == arr) ERROR_RETURN_LOG(int, "Invalid arguments");
	free(arr);
	return 0;
}

int pipe_set_type_callback(pipe_t pipe, pipe_type_callback_t callback, void* data)
{
	return RUNTIME_ADDRESS_TABLE_SYM->set_type_hook(pipe, callback, data);
}
