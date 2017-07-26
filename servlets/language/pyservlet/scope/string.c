/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>

#include <pstd.h>
#include <pstd/types/string.h>
#include <pservlet.h>

#include <scope/object.h>
#include <scope/string.h>

static void* _create(PyObject* args)
{
	const char* str = NULL;
	if(!PyArg_ParseTuple(args, "s", &str)) return NULL;
	if(NULL == str) return NULL;

	size_t len = strlen(str);
	pstd_string_t* ret = pstd_string_new(len + 1);
	if(ERROR_CODE(size_t) == pstd_string_write(ret, str, len))
	{
		pstd_string_free(ret);
		return NULL;
	}

	return ret;
}

static int _dispose(void* object)
{
	pstd_string_t* str = (pstd_string_t*)object;

	return pstd_string_free(str);
}

static scope_token_t _commit(void* object)
{
	pstd_string_t* str = (pstd_string_t*)object;

	return pstd_string_commit(str);
}

static PyObject* _get_value(PyObject* self, PyObject* args)
{
	(void)self;
	PyObject* obj;
	if(!PyArg_ParseTuple(args, "O", &obj))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	const pstd_string_t* str = (const pstd_string_t*)scope_object_retrieve(SCOPE_OBJECT_TYPE_STRING, obj);
	if(NULL == str)
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot retrieve the scope token");
		return NULL;
	}

	return PyString_FromString(pstd_string_value(str));
}

static scope_object_ops_t _ops = {
	.name = "RLS_String",
	.create = _create,
	.dispose = _dispose,
	.commit = _commit
};

static PyMethodDef _methods[] = {
	{"get_value", _get_value, METH_VARARGS, "Get the string value of the RLS string object"},
	{}
};

int scope_string_init(PyObject* module)
{
	if(ERROR_CODE(int) == scope_object_register_type_ops(SCOPE_OBJECT_TYPE_STRING, _ops))
	    ERROR_RETURN_LOG(int, "Cannot register the type callback for the string RLS object");

	PyObject* rls_module = Py_InitModule("pservlet.RLS_String", _methods);
	if(NULL == rls_module)
	    ERROR_RETURN_LOG(int, "Cannot create  pservlet.RLS_String module");

	if(-1 == PyModule_AddObject(module, "RLS_String", rls_module))
	{
		Py_DECREF(rls_module);
		ERROR_RETURN_LOG(int, "Cannot add psevlet.RLS_String module");
	}

	return 0;
}
