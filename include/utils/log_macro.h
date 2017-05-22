/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file log_macro.h
 * @brief this file contains the define of all the log related macro, and it's designed to be
 *        reused also in servlet logging
 **/

#ifndef __LOG_MACRO_H__
#include <config.h>
#define __LOG_MACRO_H__
/** @brief log levels */
enum{
	/** Use this level when something would stop the program */
	FATAL,
	/** Error level, the routine can not continue */
	ERROR,
	/** Warning level, the routine can continue, but something may be wrong */
	WARNING,
	/** Notice level, there's no error, but something you should notice */
	NOTICE,
	/** Info level, provide some information */
	INFO,
	/** Trace level, trace the program routine and behviours */
	TRACE,
	/** Debug level, detail information used for debugging */
	DEBUG
};

/** @brief helper macros for write a log, do not use it directly */
#define __LOG__(level,fmt,arg...) do{\
	    __LOG_WRITE__(level,__FILE__,__FUNCTION__,__LINE__,fmt, ##arg);\
}while(0)

#define __LOG_NOP__ do{}while(0)

#define __LOG_ERRNO__(LEVEL, fmt, arg...) LOG_##LEVEL(fmt": %s", ##arg, strerror(errno))

#ifndef LOG_LEVEL
#   define LOG_LEVEL 6
#endif

#if LOG_LEVEL >= 0
/**
*  @brief print a fatal log
*  @param fmt	formating string
*  @param arg arguments
*  @return nothing
**/
#	define LOG_FATAL(fmt,arg...) __LOG__(FATAL,fmt,##arg)
#	define LOG_FATAL_ENABLED
#else
#	define LOG_FATAL(...) __LOG_NOP__
#endif

/**
 * @brief print a fatal errno message
 * @param fmt the formating string
 * @param arg the arguments
 * @return nothing
 **/
#define LOG_FATAL_ERRNO(fmt, arg...) __LOG_ERRNO__(FATAL, fmt, ##arg)

#if LOG_LEVEL >= 1
/** @brief print a error log
*  @param fmt	formating string
*  @param arg arguments
*  @return nothing
*/
#	define LOG_ERROR(fmt,arg...) __LOG__(ERROR,fmt,##arg)
#	define LOG_ERROR_ENABLED
#else
#	define LOG_ERROR(...) __LOG_NOP__
#endif
/**
 * @brief print an errno message in error level
 * @param fmt the formating string
 * @param arg the arguments
 * @return nothing
 **/
#define LOG_ERROR_ERRNO(fmt, arg...) __LOG_ERRNO__(ERROR, fmt, ##arg)

#if LOG_LEVEL >= 2
/** @brief print a warning log
*  @param fmt	formating string
*  @param arg arguments
*  @return nothing
*/
#	define LOG_WARNING(fmt,arg...) __LOG__(WARNING,fmt,##arg)
#	define LOG_WARNING_ENABLED
#else
#	define LOG_WARNING(...) __LOG_NOP__
#endif

/**
 * @brief print an errno message in warning level
 * @param fmt the formating string
 * @param arg the arguments
 * @return nothing
 **/
#define LOG_WARNING_ERRNO(fmt, arg...) __LOG_ERRNO__(WARNING, fmt, ##arg)

#if LOG_LEVEL >= 3
/** @brief print a notice log
*  @param fmt	formating string
*  @param arg arguments
*  @return nothing
*/
#	define LOG_NOTICE(fmt,arg...) __LOG__(NOTICE,fmt,##arg)
#	define LOG_NOTICE_ENABLED
#else
#	define LOG_NOTICE(...) __LOG_NOP__
#endif

/**
 * @brief print an errno message in notice level
 * @param fmt the formating string
 * @param arg the arguments
 * @return nothing
 **/
#define LOG_NOTICE_ERRNO(fmt, arg...) __LOG_ERRNO__(NOTICE, fmt, ##arg)

#if LOG_LEVEL >= 4
/** @brief print a info log
*  @param fmt	formating string
*  @param arg arguments
*  @return nothing
*/
#	define LOG_INFO(fmt,arg...) __LOG__(INFO,fmt,##arg)
#	define LOG_INFO_ENABLED
#else
#	define LOG_INFO(...) __LOG_NOP__
#endif

/**
 * @brief print an errno message in info level
 * @param fmt the formating string
 * @param arg the arguments
 * @return nothing
 **/
#define LOG_INFO_ERRNO(fmt, arg...) __LOG_ERRNO__(INFO, fmt, ##arg)

#if LOG_LEVEL >= 5
/** @brief print a trace log
*  @param fmt	formating string
*  @param arg arguments
*  @return nothing
*/
#	define LOG_TRACE(fmt,arg...) __LOG__(TRACE,fmt,##arg)
#	define LOG_TRACE_ENABLED
#else
#	define LOG_TRACE(...) __LOG_NOP__
#endif

/**
 * @brief print an errno message in trace level
 * @param fmt the formating string
 * @param arg the arguments
 * @return nothing
 **/
#define LOG_TRACE_ERRNO(fmt, arg...) __LOG_ERRNO__(TRACE, fmt, ##arg)

#if LOG_LEVEL >= 6
/** @brief print a debug log
*  @param fmt	formating string
*  @param arg arguments
*  @return nothing
*/
#	define LOG_DEBUG(fmt,arg...) __LOG__(DEBUG,fmt,##arg)
#	define LOG_DEBUG_ENABLED
#else
#	define LOG_DEBUG(...) __LOG_NOP__
#endif

/**
 * @brief print an errno message in debug level
 * @param fmt the formating string
 * @param arg the arguments
 * @return nothing
 **/
#define LOG_DEBUG_ERRNO(fmt, arg...) __LOG_ERRNO__(DEBUG, fmt, ##arg)

#endif /*__LOG_MACRO_H__*/
