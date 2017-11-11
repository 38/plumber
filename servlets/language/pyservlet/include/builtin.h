/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The builtin function implementations for pyservlet
 * @file pyservlet/include/builtin.h
 **/
#ifndef __PYSERVLET_BUILTIN_H__
#define __PYSERVLET_BUILTIN_H__

/**
 * @brief Intiailize the module that contains all the builtin functions
 * @return The newly created python module object or NULL on error
 **/
PyObject* builtin_init_module(void);

#endif
