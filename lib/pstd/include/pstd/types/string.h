/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the standard string buffer request local scope data implementation
 * @details This is the implementaiton for the string buffer data type for the standard
 *          RLS string, which we can used to pass a reference of string via the pipe,
 *          to avoid copying the entire string again and again. <br/>
 *          When a string buffer is created, the servlet can register it to the RLS and
 *          get a RLS token for the string, then the RLS token can be written to the pipe,
 *          so that the downstream can get the token and use the API to get the data from
 *          the RLS
 * @todo    This implementaiton can not across the machine boarder (Because it uses the customized
 *          callback functions) We need to make this a built-in RLS type once we start working on
 *          the strong-typed pipes
 * @file    pstd/include/types/string.h
 **/
#ifndef __PSTD_TYPES_STRING_H__
#define __PSTD_TYPES_STRING_H__

/**
 * @brief the string buffer type
 **/
typedef struct _pstd_string_t pstd_string_t;

/**
 * @brief create a new pstd string buffer
 * @param initcap the initial capacity of the string bufer
 * @return the newly created pstd string buffer, NULL on error case
 **/
pstd_string_t* pstd_string_new(size_t initcap);

/**
 * @brief dispose a used string buffer
 * @param str the used string buffer
 * @return status code
 **/
int pstd_string_free(pstd_string_t* str);

/**
 * @brief get a shared string buffer from the RLS
 * @param token the RLS token
 * @return the target string buffer, NULL on error case
 **/
const pstd_string_t* pstd_string_from_rls(scope_token_t token);

/**
 * @brief get the underlying string from the string buffer
 * @param str the string buffer
 * @return the underlying string, error NULL on error
 **/
const char* pstd_string_value(const pstd_string_t* str);

/**
 * @brief get the number of bytes has been written to the string buffer
 * @param str the string buffer
 * @return the size of the string buffer or error code
 **/
size_t pstd_string_length(const pstd_string_t* str);

/**
 * @brief make a writable copy of the given token
 * @param token the token to copy
 * @param token_buf the buffer used to return the token to the writable copy
 * @return the pointer to the writable copy
 * @note this is the function that implmenets the copy-on-write functionality
 **/
pstd_string_t* pstd_string_copy_rls(scope_token_t token, scope_token_t* token_buf);

/**
 * @brief commit a string buffer to the RLS
 * @param str the string buffer to commit
 * @return the RLS token for the buffer or error code
 **/
scope_token_t pstd_string_commit(pstd_string_t* str);

/**
 * @brief write the bytes to the buffer
 * @param str the string buffer
 * @param data the data pointer to write
 * @param size the size of the data section
 * @return the number of bytes has been written, NULL on error case
 **/
size_t pstd_string_write(pstd_string_t* str, const char* data, size_t size);

/**
 * @brief print the formated information to the string
 * @param str the string buffer
 * @param fmt the format string
 * @return the number of bytes has been written, NULL on error case
 **/
size_t pstd_string_printf(pstd_string_t* str, const char* fmt, ...)
    __attribute__((format (printf, 2, 3)));

#endif /* __PSTD_TYPES_STRING_H__ */
