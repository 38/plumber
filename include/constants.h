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

#ifdef __LINUX__
#	include <linux/limits.h>
#else
#	error("unsupported system type")
#endif

#define __CONSTANTS_XSTR__(s) __CONSTANTS_STR__(s)
#define __CONSTANTS_STR__(s) #s

/** @brief the stringified symbol name for the servlet metadata */
#define RUNTIME_SERVLET_DEFINE_STR __CONSTANTS_XSTR__(RUNTIME_SERVLET_DEFINE_SYM)

/** @brief stringified SERVLET_ADDRESS_TABLE */
#define RUNTIME_ADDRESS_TABLE_STR __CONSTANTS_XSTR__(RUNTIME_ADDRESS_TABLE_SYM)

#ifndef RUNTIME_SERVLET_FILENAME_PREFIX
#	ifdef __LINUX__
/** @brief the prefix for the servlet filename */
#		define RUNTIME_SERVLET_FILENAME_PREFIX "lib"
#	endif /*__LINUX__ */
/* TODO: Define the prefix for other OS */
#endif /* RUNTIME_SERVLET_FILENAME_PREFIX */

#ifndef RUNTIME_SERVLET_FILENAME_SUFFIX
#	ifdef __LINUX__
/** @brief the suffix for the servlet filename */
#		define RUNTIME_SERVLET_FILENAME_SUFFIX ".so"
#	endif /*__LINUX__ */
/* TODO: Define the prefix for other OS */
#endif /* RUNTIME_SERVLET_FILENAME_SUFFIX */

/** @brief the typename for the dummy type header of an untyped pipe */
#ifndef UNTYPED_PIPE_HEADER
#	define UNTYPED_PIPE_HEADER "plumber/base/Raw"
#endif /* UNTYPED_PIPE_HEADER */

#endif /* __CONSTANTS_H__ */
