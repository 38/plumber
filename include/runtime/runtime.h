/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the top level include file for the runtime directory
 * @file runtime/runtime.h
 **/
#ifndef __PLUMBER_RUNTIME_H__
#define __PLUMBER_RUNTIME_H__

#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

/**
 * @brief initialize the runtime directory
 * @return error code < 0 indicates error
 **/
int runtime_init();

/**
 * @brief finalize the runtime director
 * @return error code < 0 indicates error
 **/
int runtime_finalize();

#endif /* __PLUMBER_RUNTIME_H__ */
