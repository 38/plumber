/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file testenv.h
 * @brief the framework for unit test
 * @note the framework will initialize plumber system for the unit test,
 *       so DO NOT REINITIALIZED PLUMBER IN THE TEST FILE
 **/
#ifndef __TESTENV_TESTENV_H__
#define __TESTENV_TESTENV_H__

/* We don't actually care about the test case */
#pragma GCC diagnostic ignored "-Wshadow"

#include <stdlib.h>
#include <string.h>

#include <plumber.h>
#include <error.h>
#include <utils/log.h>
/**
 * @brief the function pointer to a test case
 * @return < 0 if test case fails
 **/
typedef int (*test_case_func_t)(void);
/**
 * @brief the data structure for a test case
 **/
typedef struct {
	const char*		 name; /* the name of the test case */
	test_case_func_t func; /* the function pointer to the test case */
} test_case_t;
/**
 * @brief this macro is used at the begining of the test case table
 **/
#define TEST_LIST_BEGIN test_case_t test_list[] = {
	/**
	* @brief this macro is used at the end of the test case table
	**/
#define TEST_LIST_END ,{NULL, NULL}}
/**
 * A setup function that does nothing
 **/
#define DEFAULT_SETUP    int setup(void){return 0;}
/**
 * A teardown function that does nothing
 **/
#define DEFAULT_TEARDOWN int teardown(void){return 0;}

/**
 * @brief create a test case in the test case list
 * @param func the test function used in the test case
 **/
#define TEST_CASE(func) {#func, func}

/**
 * @brief Make an assertion in test cases
 * @param cond the condition expression to check
 * @param cleanup the code to clean up when error
 **/
#define ASSERT(cond, cleanup) do{\
	if(!(cond)) \
	{\
		LOG_ERROR("Assertion failure `%s'", #cond);\
		cleanup;\
		return -1;\
	}\
} while(0)

/**
* @brief assert two strings are equal
* @param left the left string expression
* @param right the right string expression
* @param cleanup the cleanup code when error
**/
#define ASSERT_STREQ(left, right, cleanup) ASSERT(strcmp((left), (right)) == 0, cleanup)

/**
* @brief assert a string is not null
* @param ptr the pointer expression that is to evaluate
* @param cleanup the cleanup code when error
**/
#define ASSERT_PTR(ptr, cleanup) ASSERT((ptr) != NULL, cleanup)

/**
* @brief assert the status code is OK
* @param status the status code to evaluate
* @param cleanup the cleanup code used when error
**/
#define ASSERT_OK(status, cleanup) ASSERT((status) != ERROR_CODE(int), cleanup)

/**
 * @brief assert the return value is OK
 * @param type the type of the return value
 * @param ret the value
 * @param cleanup the cleanup code
 **/
#define ASSERT_RETOK(type, ret, cleanup) ASSERT((ret) != ERROR_CODE(type), cleanup)

/**
* @brief the place holder, that is used when the code doesn't need a clean up code
**/
#define CLEANUP_NOP

/**
* @brief turn off the memory check
* @note you have to specify allow disable memory check explictly, otherwise this macro does nothing
**/
#ifdef ALLOW_IGNORE_MEMORY_LEAK
#   define IGNORE_MEMORY_LEAK() do{\
	extern void __disable_memory_check(void);\
	__disable_memory_check();\
} while(0)
#else /* ALLOW_IGNORE_MEMORY_LEAK */
#   define IGNORE_MEMORY_LEAK()
#endif /* ALLOW_IGNORE_MEMORY_LEAK */

/**
* @brief because some library has unfixed bug, so we need add some exemption on this case
* @return nothing
**/
void expected_memory_leakage(void);

#endif
