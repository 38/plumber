/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

/**
 * @brief the module header that declare this module
 * @file tcp/module.h
 **/
#ifndef __MODULE_TCP_MODULE_H__
#define __MODULE_TCP_MODULE_H__

extern itc_module_t module_tcp_module_def;

/**
 * @brief get the TCP conection pool
 * @param context The module context to inspect
 * @note This function should only used for debugging purpose
 * @return the connection pool pointer
 **/
void* module_tcp_module_get_pool(void* context);

/**
 * @brief set the listening port for this context
 * @param context The module context to configure
 * @param port The port the TCP module would like to listen
 * @return status code
 * @note This function should only used for debugging purpose
 **/
int module_tcp_module_set_port(void* context, uint16_t port);

#endif /* __MODULE_HTTP_MODULE_H__ */
