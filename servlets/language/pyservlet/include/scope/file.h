/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The RLS file objet wrapper
 * @file pyservlet/include/scope/string.h
 **/
#ifndef __SCOPE_FILE_H__
#define __SCOPE_FILE_H__

/**
 * @brief Intialize the Python wrapper string object
 * @param module The module where we want to initialize the module
 * @return status code
 **/
int scope_file_init(PyObject* module);

#endif
