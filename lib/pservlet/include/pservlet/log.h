/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

/**
 * @brief the utils that is used for servlet logging
 * @file pservlet/include/pservlet/log.h
 **/
#ifndef __LOG_H__
#define __LOG_H__


/** @brief	the implementation of write a log
 *  @param	level	the log level
 *  @param	file	the file name of the source code
 *  @param	function	function name
 *  @param	line	line number
 *  @param	fmt		formating string
 *  @return nothing
 */
void log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
    __attribute__((format (printf, 5, 6)))
	__attribute__((visibility ("hidden")));

#define __LOG_WRITE__ log_write
#include <utils/log_macro.h>

#endif /*__LOG_H__*/
