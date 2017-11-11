/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file plumber.h
 * @brief the top level include file of plumber
 **/
#ifndef __PLUMBER_H__
#define __PLUMBER_H__

#	ifdef __cplusplus
extern "C" {
#	endif /* __cplusplus__ */

#include <stdint.h>

#include <api.h>

#include <itc/itc.h>

#include <runtime/runtime.h>

#include <sched/sched.h>

#include <lang/lang.h>

	/**
	* @brief global intialization
	* @return status code
	**/
	int plumber_init(void);

	/**
	* @brief global finalization
	* @return status code
	**/
	int plumber_finalize(void);

	/**
	* @brief get the version of libplumber
	* @return the version string
	**/
	const char* plumber_version(void);

#	ifdef __cplusplus__
}
#	endif /* __cplusplus__ */
#endif /* __PLUMBER_H__ */
