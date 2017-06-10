/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The log utilities for the PSSVM 
 * @file  pss/log.h
 **/
#ifndef __PSS_LOG_H__
#define __PSS_LOG_H__

/**
 * @brief The function pointer type for logging
 **/
typedef void (*pss_log_write_func_t)(int level, const char* file, const char* func, int line, const char* fmt, ...);

/**
 * @brief Set the logging callback function
 * @param func The logging function we want to use
 * @return status code
 **/
int pss_log_set_write_callback(pss_log_write_func_t func);

/**
 * @brief Log a message
 * @param level The log level
 * @param file  The source code file name
 * @param function The function name
 * @param line The line number
 * @param fmt The format string
 * @return nothing
 **/
void pss_log_write(int level, const char* file, const char* function, int line, const char* fmt, ...) 
	__attribute__((format (printf, 5, 6)));

#define __LOG_WRITE__ pss_log_write
#include <utils/log_macro.h>

#endif /* __PSS_LOG_H__ */
