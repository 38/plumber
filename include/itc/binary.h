/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief ITC module binary management
 * @details This file provides the function that is used to manage the module binary
 *          Both built-in module and external module in a same way
 * @file itc/binary.h
 **/
#ifndef __PLUMBER_ITC_BINARY_H__
#define __PLUMBER_ITC_BINARY_H__

/**
 * @brief the initialization function for this file
 * @return status code
 **/
int itc_binary_init(void);

/**
 * @brief the finalization function for this file
 * @return status code
 **/
int itc_binary_finalize(void);

/**
 * @brief search the module binary by its name
 * @param name the name of the module
 * @return the module binary has been found, or NULL on error cases
 **/
const itc_module_t* itc_binary_search_module(const char* name);

#endif /* __PLUMBER_ITC_BINARY_H__ */

