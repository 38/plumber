/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the high level bufferred pipe IO
 * @file pstd/include/pstd/bio.h
 **/
#ifndef __PSTD_BIO_H__
#define __PSTD_BIO_H__

/**
 * @brief the high level level bufferered pipe IO
 **/
typedef struct _pstd_bio_t pstd_bio_t;

/**
 * @brief create new BIO object
 * @param pipe the pipe used by this BIO object
 * @note the BIO object uses the auto memory allocator, so free is optional
 * @return the newly create pipe
 **/
pstd_bio_t* pstd_bio_new(pipe_t pipe);

/**
 * @brief flush a BIO buffer
 * @param pstd_bio the target pstd_bio object
 * @return status code
 **/
int pstd_bio_flush(pstd_bio_t* pstd_bio);

/**
 * @brief free a pstd_bio object and flush unwritten buffer
 * @param pstd_bio the pstd_bio object
 * @return status code
 **/
int pstd_bio_free(pstd_bio_t* pstd_bio);

/**
 * @brief get the pipe that this BIO object operates
 * @param pstd_bio the target BIO
 * @return the result pipe or status code
 **/
pipe_t pstd_bio_pipe(pstd_bio_t* pstd_bio);

/**
 * @brief set the BIO buffer size
 * @param pstd_bio the target BIO object
 * @param size the size of the buffer
 * @return status code
 **/
int pstd_bio_set_buf_size(pstd_bio_t* pstd_bio, size_t size);

/**
 * @brief read structs from the BIO object
 * @param pstd_bio the target pstd_bio object
 * @param ptr the target point
 * @param size the size of the buffer
 * @return the size of bytes has been read from the BIO object, or error code
 **/
size_t pstd_bio_read(pstd_bio_t* pstd_bio, void* ptr, size_t size);

/**
 * @brief get a single char from the bufferred IO object
 * @param pstd_bio the target BIO object
 * @param ch the result char
 * @return number of char returned, or error code
 **/
int pstd_bio_getc(pstd_bio_t* pstd_bio, char* ch);

/**
 * @brief check if the BIO object has no more data to read
 * @param pstd_bio the target BIO object
 * @return the result or status code
 **/
int pstd_bio_eof(pstd_bio_t* pstd_bio);

/**
 * @brief write structs to the BIO object
 * @param pstd_bio the target pstd_bio object
 * @param ptr the data to write
 * @param size the size of the buffer
 * @return the number of bytes has been actually written
 **/
size_t pstd_bio_write(pstd_bio_t* pstd_bio, const void* ptr, size_t size);

/**
 * @brief write the RLS content to the pipe
 * @param pstd_bio the target pstd_bio object
 * @param token the token to write
 * @note this function will make sure all the bytes for the token be written
 * @return status code
 **/
int pstd_bio_write_scope_token(pstd_bio_t* pstd_bio, scope_token_t token);

/**
 * @brief formated print to BIO
 * @param pstd_bio the target pstd_bio
 * @param fmt the formating string
 * @return number of bytes has written
 **/
size_t pstd_bio_printf(pstd_bio_t* pstd_bio, const char* fmt, ...)
    __attribute__((format (printf, 2, 3)));

/**
 * @brief write a string to a BIO object
 * @param pstd_bio the target BIO
 * @param str the line to write
 * @return the number of bytes has written
 **/
size_t pstd_bio_puts(pstd_bio_t* pstd_bio, const char* str);

/**
 * @brief write a signle char to a BIO object
 * @param pstd_bio the target BIO object
 * @param ch the char to write
 * @return the number of bytes has written
 **/
int pstd_bio_putc(pstd_bio_t* pstd_bio, char ch);
#endif /* __PSTD_BIO_H__ */
