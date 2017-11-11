/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the module header that declare this module
 * @file test/module.h
 **/
#ifndef __MODULE_TEST_MODULE_H__
#define __MODULE_TEST_MODULE_H__

extern itc_module_t module_test_module_def;


/**
 * @brief set the mocked request
 * @param data the data to copy
 * @param count the size of the data
 * @return status code
 **/
int module_test_set_request(const void* data, size_t count);

/**
 * @brief get the mocked response
 * @return the result data, NULL if error
 **/
const void* module_test_get_response(void);
#endif /* __MODULE_TEST_MODULE_H__ */
