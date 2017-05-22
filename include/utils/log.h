/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file utils/log.h
 * @brief function and macros for logging. (needs initalization and fianlization)
 * @details  You can use LOG_<LOG_LEVEL> to output a log in the code.
 *
 * 			 In program you can use LOG_xxx to print a log
 *
 * 			 There are 6 log levels : fatal, error, warning, notice, info, trace, debug
 *
 * 			 You can use LOG_LEVEL to set above which level, the log should display.
 *
 * 			 LOG_LEVEL=6 means record all logs, LOG_LEVEL=0 means only fatals.
 *
 * 			 Config file log.conf is used for redirect log to a file. For each log level, we
 * 			 can define an output file, so that we can seperately record log in different  level in
 * 			 different files.
 */
#ifndef __LOG_H__
#define __LOG_H__

#include <stdarg.h>

/** @brief initlaization
 *  @return nothing
 */
int log_init();
/** @brief initlaization
 *  @return nothing
 */
int log_finalize();

/** @brief	the implementation of write a log
 *  @param	level	the log level
 *  @param	file	the file name of the source code
 *  @param	function	function name
 *  @param	line	line number
 *  @param	fmt		formating string
 *  @return nothing
 */
void log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
    __attribute__((format (printf, 5, 6)));

/**
 * @brief this file mainly used for the servlet logging, because there's another wrap around that
 *  @param	level	the log level
 *  @param	file	the file name of the source code
 *  @param	function	function name
 *  @param	line	line number
 *  @param	fmt		formating string
 *  @param  ap      the VA_ARGS
 * @return nothing
 **/
void log_write_va(int level, const char* file, const char* function, int line, const char* fmt, va_list ap);

#define __LOG_WRITE__ log_write
#include <utils/log_macro.h>

#endif
