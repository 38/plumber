/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the on exit funcionality, which allows user space code to reigster a callback function which
 *        will be called when the plumber service exits. This is used to clean up the global variable
 *        in the plumber user-space code. <br/>
 *        For example the user-space cache system uses this function clean up the cache memory when the
 *        framework exits
 * @file pstd/include/pstd/onexit.h
 **/
#ifndef __PSTD_ONEXIT_H__
#define __PSTD_ONEXIT_H__

/**
 * @brief the callback function which will be called when libplumber finalize
 * @note the callback also responsible for disposing the data pointer if it requires to dispose
 * @param data the additional data for this callback function
 * @return nothing
 **/
typedef void (*pstd_onexit_callback_t)(void* data);

/**
 * @brief register the callback function that will be called when the libplumber exits, this require pssm loaded
 * @details if there are multiple callback is registered in the system, the callback function will be called the
 *          last registered function first. (The callback list is a stack)
 * @param callback the callback function
 * @param data the optional addtional data
 * @note the callback may also responsible to dispose the data pointer if the data pointer requires a free <br/>
 *       If the function returns a failure, the data pointer may need to be disposed by the caller because the ownership
 *       isn't taken unless it returns a success
 * @return status code
 **/
int pstd_onexit(pstd_onexit_callback_t callback, void* data);

#endif /* __PSTD_ONEXIT_H__ */
