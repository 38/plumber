/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdarg.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <utils/string.h>

void string_buffer_open(char* buffer, size_t size, string_buffer_t* res)
{
	res->buffer = buffer;
	res->size = size;
	res->result = buffer;
}

size_t string_buffer_append(const char* str, string_buffer_t* buf)
{
	if(buf->size <= 1) return 0;

	size_t s_size = strlen(str);
	if(s_size >= buf->size) s_size = buf->size - 1;

	memcpy(buf->buffer, str, s_size);

	buf->buffer += s_size;
	buf->size -= s_size;

	return s_size;
}

size_t string_buffer_appendf(string_buffer_t* buf, const char* fmt, ...)
{
	if(buf->size <= 1) return 0;

	va_list ap;
	va_start(ap,fmt);
	int rc = vsnprintf(buf->buffer, buf->size, fmt, ap);
	va_end(ap);
	if(rc < 0) return 0;

	if((int)buf->size - rc < 1) rc = (int)buf->size - 1;

	buf->buffer += (size_t)rc;
	buf->size -= (size_t)rc;

	return (size_t)rc;
}

const char* string_buffer_close(string_buffer_t* buf)
{
	if(buf->size < 1) return NULL;

	buf->buffer[0] = 0;

	buf->size --;

	return buf->result;
}

size_t string_buffer_append_range(const char* begin, const char* end, string_buffer_t* buf)
{
	if(NULL == begin || NULL == end || begin >= end || buf->size == 0) return 0;

	size_t to_write = (size_t)(end - begin);

	if(to_write + 1 > buf->size)
	    to_write = buf->size - 1;

	memcpy(buf->buffer, begin, to_write);

	buf->size -= to_write;
	buf->buffer += to_write;

	return to_write;
}
