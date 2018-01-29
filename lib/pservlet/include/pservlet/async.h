/**
 * Copyright (C) 2017-2018, Hao Hou
 **/
/**
 * @brief The header file for the async task related APIs
 * @file pservlet/include/pservlet/async.h
 **/
#ifndef __PSERVLET_ASYNC_H__
#define __PSERVLET_ASYNC_H__

/**
 * @brief The async task controlling function
 * @param handle the async handle
 * @param opcode The operation code
 * @return status code
 **/
int async_cntl(async_handle_t* handle, uint32_t opcode, ...)
    __attribute__((visibility ("hidden")));


#endif /*__PSERVLET_ASYNC_H__*/
