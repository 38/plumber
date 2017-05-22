/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the module header that declare this module
 * @file tcp/module.h
 **/
#ifndef __MODULE_TCP_MODULE_H__
#define __MODULE_TCP_MODULE_H__

extern itc_module_t module_tcp_module_def;

/**
 * @brief get the TCP connection pool
 * @return the connection pool pointer
 **/
void* module_tcp_module_get_pool(void* context);

/**
 * @brief set the listening port for this context
 **/
int module_tcp_module_set_port(void* context, uint16_t port);

#endif /* __MODULE_HTTP_MODULE_H__ */
