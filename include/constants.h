/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file constants.h
 *  @brief this file constains constants
 **/
#ifndef __PLUMBER_CONSTANTS_H__
#define __PLUMBER_CONSTANTS_H__

#include <config.h>

#include <os/const.h>

#define __CONSTANTS_XSTR__(s) __CONSTANTS_STR__(s)
#define __CONSTANTS_STR__(s) #s

/** @brief the stringified symbol name for the servlet metadata */
#define RUNTIME_SERVLET_DEFINE_STR __CONSTANTS_XSTR__(RUNTIME_SERVLET_DEFINE_SYM)

/** @brief stringified SERVLET_ADDRESS_TABLE */
#define RUNTIME_ADDRESS_TABLE_STR __CONSTANTS_XSTR__(RUNTIME_ADDRESS_TABLE_SYM)

/** @brief the typename for the dummy type header of an untyped pipe */
#ifndef UNTYPED_PIPE_HEADER
#	define UNTYPED_PIPE_HEADER "plumber/base/Raw"
#endif /* UNTYPED_PIPE_HEADER */

#endif /* __CONSTANTS_H__ */
