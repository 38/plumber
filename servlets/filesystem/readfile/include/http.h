/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The HTTP Response context
 * @file filesystem/readfile/http.h
 **/
#ifndef __HTTP_H__
#define __HTTP_H__

/**
 * @brief The HTTP context
 **/
typedef struct _http_ctx_t http_ctx_t;

/**
 * @brief Create a new http context
 * @param options The options
 * @param type_model The type model
 * @return status code
 **/
http_ctx_t* http_ctx_new(const options_t* options, pstd_type_model_t* type_model);

/**
 * @brief Execute read file create HTTP context
 * @param ctx The http context
 * @param type The type instance
 * @paraam path The path to read
 * @return status code
 **/
int http_ctx_exec(const http_ctx_t* ctx, pstd_type_instance_t* type, const char* path);

/**
 * @brief Dispose the context
 * @param ctx The HTTP context
 * @return status code
 **/
int http_ctx_free(http_ctx_t* ctx);

#endif
