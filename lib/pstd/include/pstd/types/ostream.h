/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The output stream RLS object
 * @details The stream RLS object is the object that accepts other RLS token or string and
 *          concatenate all the RLS tokens and strings into a single RLS.
 *          This is typically useful when we want to generate a gziped TCP result which we 
 *          have to write in multiple times
 * @file include/pstd/types/ostream.h
 **/
#ifndef __PSTD_OSTREAM_T__
#define __PSTD_OSTREAM_T__

#include <pservlet.h>
#include <pstd/scope.h>

/**
 * @brief The output stream object
 **/
typedef struct _pstd_ostream_t pstd_ostream_t;

/**
 * @brief Create a new PSTD output stream object
 * @return The newly created PSTD stream object
 **/
pstd_ostream_t* pstd_ostream_new(void);

/**
 * @brief Dispose a used output stream object
 * @param stream The used output stream object to dispose
 * @return status code
 * @note Dispose a committed token should be an error
 **/
int pstd_ostream_free(pstd_ostream_t* ostream);

/**
 * @brief Commit the output stream to the RLS
 * @param stream The output stream object
 * @return the RLS token for this objecct
 **/
scope_token_t pstd_ostream_commit(pstd_ostream_t* stream);

/**
 * @brief Append the given buffer to the ostream
 * @param stream The stream 
 * @param buf The memory buffer
 * @param sz The size of the buffer
 * @return status code
 **/
int pstd_ostream_write(pstd_ostream_t* stream, const void* buf, size_t sz);

/**
 * @brief Append the given buffer to the ostream
 * @note This function is very similar to pstd_ostram_write, however, it takes
 *       the ownership of the passed in pointer. Which means it will be disposed
 *       automatically by the RLS object.
 * @param buf The memory buffer
 * @param free_func The function used to free the buffer
 * @param sz The size of the buffer
 * @return status code
 **/
int pstd_ostream_write_owner_pointer(pstd_ostream_t* stream, void* buf, int (*free_func)(void*), size_t sz);

/**
 * @brief Append another RLS token to the ostream
 * @param stream The stream object
 * @param token The token to write
 * @return status code
 **/
int pstd_ostream_write_scope_token(pstd_ostream_t* stream, scope_token_t token);

/**
 * @brief printf function into the output stream
 * @param stream The target stream
 * @param fmt The format string
 * @return status code
 **/
int pstd_ostream_printf(pstd_ostream_t* stream, const char* fmt, ...)
	__attribute__((format(printf, 2, 3)));

#endif
