/**
 * Copyright (C) 2017-2018, Hao Hou
 **/
/**
 * @brief the functions related to the plumber runtime
 * @file pservlet/include/pservlet/runtime.h
 **/
#ifndef __PSERVLET_RUNTIME_H__
#define __PSERVLET_RUNTIME_H__
/**
 * @brief get the plumber version number
 * @return the plumber version number string, NULL incidates an error
 **/
const char* runtime_version(void)
    __attribute__((visibility ("hidden")));
#endif /* __PSERVLET_PLUMBER_H__ */
