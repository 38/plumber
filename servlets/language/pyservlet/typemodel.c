/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>

#include <pservlet.h>
#include <pstd.h>

#include <typemodel.h>

int typemodel_object_init(PyObject* module)
{
	if(NULL == module) 
		ERROR_RETURN_LOG(int, "Invalid arguments");

	return 0;
}
