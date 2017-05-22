/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file include/utils/string.h
 * @brief string manipuation utils
 **/
#ifndef __STRING_H__
#define __STRING_H__
#include <string.h>

/**
 * @brief a string buffer
 **/
typedef struct {
	size_t size;        /*!< the size of the string buffer */
	char*  buffer;      /*!< the pointer to actual memory used for this buffer */
	const char* result; /*!< the pointer to the result string */
} string_buffer_t;

/**
 * @brief make a string buffer
 * @param buffer the memory for this string buffer
 * @param size the size of the string buffer
 * @param res the result string buffer
 * @return nothing
 **/
void string_buffer_open(char* buffer, size_t size, string_buffer_t* res);

/**
 * @brief append a string to the string buffer (not including the \0). If the buffer is full, truncate the input string so that the output string will have a \0
 * @param str the string to append
 * @param buf the target string buffer
 * @return the number of bytes written to the buffer
 **/
size_t string_buffer_append(const char* str, string_buffer_t* buf);

/**
 * @brief Append a string to the string buffer. If the buffer is full, trucate the input string
 * @param begin The begin of the range
 * @param end   The end of the range
 * @param buf   The target string buffer
 * @return status code
 **/
size_t string_buffer_append_range(const char* begin, const char* end, string_buffer_t* buf);

/**
 * @brief append a string to the string buffer, similar to string_buffer_append, but takes format string
 * @param buf the target string buffer
 * @param fmt the formatting string
 * @return the number of bytes written to the buffer
 **/
size_t string_buffer_appendf(string_buffer_t* buf, const char* fmt, ...)
    __attribute__((format (printf, 2, 3)));


/**
 * @brief close a string buffer, append \0 in the end of string
 * @param buf the target buffer
 * @return the string built by this buffer, NULL if error
 **/
const char* string_buffer_close(string_buffer_t* buf);

#endif
