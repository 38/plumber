/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The log utilities for the libjsonschema
 * @file  jsonschema/log.h
 **/
#ifndef __JSONSCHEMA_LOG_H__
#define __JSONSCHEMA_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

	/**
	* @brief The function pointer type for logging
	**/
	typedef void (*jsonschema_log_write_func_t)(int level, const char* file, const char* func, int line, const char* fmt, va_list ap);

	/**
	* @brief Set the logging callback function
	* @param func The logging function we want to use
	* @return status code
	**/
	int jsonschema_log_set_write_callback(jsonschema_log_write_func_t func);

	/**
	* @brief Log a message
	* @param level The log level
	* @param file  The source code file name
	* @param function The function name
	* @param line The line number
	* @param fmt The format string
	* @return nothing
	**/
	void jsonschema_log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
	__attribute__((format (printf, 5, 6)));

#ifndef __LOG_WRITE__
#define __LOG_WRITE__ jsonschema_log_write
#endif

#include <utils/log_macro.h>

#ifdef __cplusplus
}
#endif

#endif /* __JSONSCHEMA_LOG_H__ */
