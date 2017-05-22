/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The logging utils
 * @file protoman/include/log.h
 **/
#ifndef __LOG_H__
#define __LOG_H__

/** @brief	the implementation of write a log, all the application using the PType library should implement this
 *          function
 *  @param	level	the log level
 *  @param	file	the file name of the source code
 *  @param	function	function name
 *  @param	line	line number
 *  @param	fmt		formating string
 *  @return nothing
 */
void log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
    __attribute__((format (printf, 5, 6)));

#define __LOG_WRITE__ log_write

#include <utils/log_macro.h>

/**
 * @brief log the libproto error
 * @param filename the filename for the callsite
 * @param lineno the lineno for the callsite
 * @return nothing
 **/
void log_libproto_error(const char* filename, int lineno);

/**
 * @brief set the log level
 * @param value the level to set
 * @return nothing
 **/
void log_level(int value);

#define LOG_LIBPROTO_ERROR_RETURN(type) do {\
	    log_libproto_error(__FILE__, __LINE__);\
	    return ERROR_CODE(type);\
    } while(0)

#define LOG_LIBPROTO_ERROR_RETURN_PTR() do {\
	    log_libproto_error(__FILE__, __LINE__);\
	    return NULL;\
    } while(0)

#define LOG_LIBPROTO_ERROR_GOTO(label) do {\
	    log_libproto_error(__FILE__, __LINE__);\
	    goto label;\
    } while(0)
#endif /* __LOG_H__ */
