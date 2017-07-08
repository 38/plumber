/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The RLS string objet wrapper
 * @file pyservlet/include/scope/string.h
 **/
#ifndef __SCOPE_STRING_H__
#define __SCOPE_STRING_H__

/**
 * @brief Intialize the Python wrapper string object
 * @param module The module where we want to initialize the module
 * @return status code
 **/
int scope_string_init(PyObject* module);

#endif
