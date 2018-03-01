/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The zlib RLS token wrapper
 * @file include/zlib_token.h
 **/
#ifndef __ZLIB_TOKEN_H__
#define __ZLIB_TOKEN_H__

typedef enum {
	ZLIB_TOKEN_FORMAT_GZIP,
	ZLIB_TOKEN_FORMAT_DEFLATE
} zlib_token_format_t;

/**
 * @brief Encode an original token to a zlib processed data
 * @param data_token The data token to encode
 * @param format The format we use, either gzip or deflate
 * @param level The compression level
 * @return The result token
 **/
scope_token_t zlib_token_encode(scope_token_t data_token, zlib_token_format_t format, int level);

#endif
