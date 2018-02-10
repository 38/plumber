/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the module header that declare this module
 * @file tls/module.h
 **/
#include <constants.h>

#if !defined(__MODULE_TLS_MODULE_H__) && MODULE_TLS_ENABLED
#define __MODULE_TLS_MODULE_H__

extern itc_module_t module_tls_module_def;

/**
 * @brief The internal TLS context maintained by this module
 **/
typedef struct _module_tls_module_conn_data_t module_tls_module_conn_data_t;

/**
 * @brief Decrease the reference counter of the TLS context
 * @param ctx The context
 * @return status code
 **/
int module_tls_module_conn_data_release(module_tls_module_conn_data_t* ctx);

#endif /* __MODULE_HTTP_MODULE_H__ */
