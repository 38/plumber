/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file utils.h
 * @brief This directory contains all the shared code, like hash table, vectors, string utilities, etc.
 * @details it is the only exception in the naming convention that forces the
 *        directory name prefixed to the function and type name.
 *        File under this directory can have a init and finalize function, and the top
 *        level should be util_init and util_finalize.
 * @note include this header file outside plumber.c is not recommended, because most parts of the project does not
 *       likely use all the utils under this folder.
 *       The only purpose of this top level file is initialization. So this file won't includes all the headers under
 *       the directory.
**/

#ifndef __UTILS_H__
/**
 * @brief initialize utils
 * @return <0 when error
 **/
int utils_init();

/**
 * @brief finalize utils
 * @return <0 when error
 **/
int utils_finalize();

#endif /*__UTILS_H__*/
