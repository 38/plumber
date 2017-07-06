/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The builtin object for type models
 * @file typemodel.h
 **/

#ifndef __PYSERVLET_TYPEMODEL_H__
#define __PYSERVLET_TYPEMODEL_H__

/**
 * @brief Add the typemodel builtin type to the python module
 * @param module The module to add
 * @return status code
 **/
int typemodel_object_init(PyObject* module);

#endif
