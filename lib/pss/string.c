/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <fallthrough.h>
#include <error.h>

#include <pss/log.h>
#include <pss/string.h>
#include <pss/bytecode.h>
#include <pss/value.h>

/**
 * @brief Make a new string
 * @note This is actually a dummy function
 * @return The string itself
 **/
static void* _mkval(void* str)
{
	return str;
}

/**
 * @brief Dispose a used string
 * @param mem The memeory the string occupies
 * @return status code
 **/
static int _free(void* mem)
{
	if(NULL == mem) ERROR_RETURN_LOG(int, "Invalid arguments");

	free(mem);

	return 0;
}

/**
 * @brief string.toString
 * @param str The string object
 * @param buf The buffer
 * @param bufsize The size of the buffer
 * @return The string itself
 **/
static const char* _tostr(const void* str, char* buf, size_t bufsize)
{
	(void)buf;
	(void)bufsize;
	return str;
}

int pss_string_init()
{
	pss_value_ref_ops_t ops = {
		.mkval = _mkval,
		.free  = _free,
		.tostr = _tostr
	};

	return pss_value_ref_set_type_ops(PSS_VALUE_REF_TYPE_STRING, ops);
}

int pss_string_finalize()
{
	return 0;
}

char* pss_string_concat(const char* left, const char* right)
{
	if(NULL == left || NULL == right)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t left_len = strlen(left);
	size_t right_len = strlen(right);
	size_t len = left_len + right_len + 1;
	char* ret = (char*)malloc(len);
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new string");

	memcpy(ret, left, left_len);
	memcpy(ret + left_len, right, right_len + 1);

	return ret;
}

char* pss_string_literal(const char* str, char* buf, size_t sz)
{
	if(NULL == str || (NULL != buf && sz < 1))
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t actual_size = 0;
	const char* this;
	for(this = str; *this; this ++)
		switch(*this)
	    {
		    case '\a':
		    case '\b':
		    case '\f':
		    case '\r':
		    case '\n':
		    case '\t':
		    case '\v':
		    case '\'':
		    case '\"':
		    case '\?':
		    case '\\':
			    actual_size ++;
			    FALLTHROUGH("For the escape char, we need an additional space for the backslash");
		    default:
		        actual_size ++;
	    }

	int truncate = 0;
	if(NULL != buf)
		truncate = (actual_size + 3 > sz); /* Because we need \0 and a pair of quote */
	else if(NULL == (buf = (char*)malloc(sz = actual_size + 3)))
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the string literal representation");

	if(truncate)
	{
		buf[0] = 0;
		return buf;
	}

	char* ret = buf;
	*(buf++) = '"';

#define _ESC(ch) *(buf++) = '\\'; *(buf++) = ch; break
	for(this = str; *this; this ++)
		switch(*this)
	    {
		    case '\a': _ESC('a');
		    case '\b': _ESC('b');
		    case '\f': _ESC('f');
		    case '\r': _ESC('r');
		    case '\n': _ESC('n');
		    case '\t': _ESC('t');
		    case '\v': _ESC('v');
		    case '\\': _ESC('\\');
		    case '\'': _ESC('\'');
		    case '\"': _ESC('\"');
		    case '\?': _ESC('\?');
		    default: *(buf++) = *this;
	    }
#undef _ESC
	buf[0] = '\"';
	buf[1] = 0;
	return ret;
}
