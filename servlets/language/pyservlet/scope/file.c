/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>

#include <pstd.h>
#include <pstd/types/file.h>
#include <pservlet.h>

#include <scope/object.h>
#include <scope/file.h>

static void* _create(PyObject* args)
{
	const char* filename = NULL;
	if(!PyArg_ParseTuple(args, "s", &filename)) return NULL;
	if(NULL == filename) return NULL;

	pstd_file_t* ret = pstd_file_new(filename);  

	return ret;
}

static int _dispose(void* object)
{
	pstd_file_t* file = (pstd_file_t*)object;

	return pstd_file_free(file);
}

static scope_token_t _commit(void* object)
{
	pstd_file_t* str = (pstd_file_t*)object;

	return pstd_file_commit(str);
}

static PyObject* _size(PyObject* self, PyObject* args)
{
	(void)self;
	PyObject* obj;
	if(!PyArg_ParseTuple(args, "O", &obj)) return NULL;

	const pstd_file_t* file = (const pstd_file_t*)scope_object_retrieve(SCOPE_OBJECT_TYPE_FILE, obj);
	if(NULL == file) 
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	size_t size = pstd_file_size(file);

	if(ERROR_CODE(size_t) == size)
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot get the size of the file");
		return NULL;
	}

	return Py_BuildValue("K", (unsigned long long)size);
}

static PyObject* _exist(PyObject* self, PyObject* args)
{
	(void)self;
	PyObject* obj;
	if(!PyArg_ParseTuple(args, "O", &obj)) return NULL;

	const pstd_file_t* file = (const pstd_file_t*)scope_object_retrieve(SCOPE_OBJECT_TYPE_FILE, obj);
	if(NULL == file) 
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	int rc = pstd_file_exist(file);

	if(ERROR_CODE(int) == rc)
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot check if the file exists");
		return NULL;
	}

	return Py_BuildValue("O", rc ? Py_True : Py_False);
}

static PyObject* _open(PyObject* self, PyObject* args)
{
	(void)self;
	PyObject* obj;
	const char *mode = "r";
	if(!PyArg_ParseTuple(args, "O|s", &obj, &mode)) return NULL;

	const pstd_file_t* file = (const pstd_file_t*)scope_object_retrieve(SCOPE_OBJECT_TYPE_FILE, obj);
	if(NULL == file) 
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	FILE* cfile = pstd_file_open(file, mode);
	const char* filename = pstd_file_name(file);
	if(NULL == mode)
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot open file");
		return NULL;
	}

	return PyFile_FromFile(cfile, (char*)filename, (char*)mode, fclose);
}

static scope_object_ops_t _ops = {
	.name = "RLS_File",
	.create = _create,
	.dispose = _dispose,
	.commit = _commit
};

static PyMethodDef _methods[] = {
	{"size", _size, METH_VARARGS, "Get the size of the file"},
	{"exists", _exist, METH_VARARGS, "If this file exists"},
	{"open", _open, METH_VARARGS, "Open this  file"},
	{}
};

int scope_file_init(PyObject* module)
{
	if(ERROR_CODE(int) == scope_object_register_type_ops(SCOPE_OBJECT_TYPE_FILE, _ops))
		ERROR_RETURN_LOG(int, "Cannot register the type callback for the string RLS object");

	PyObject* rls_module = Py_InitModule("pservlet.RLS_File", _methods);
	if(NULL == rls_module)
		ERROR_RETURN_LOG(int, "Cannot create  pservlet.RLS_File module");

	if(-1 == PyModule_AddObject(module, "RLS_File", rls_module))
	{
		Py_DECREF(rls_module);
		ERROR_RETURN_LOG(int, "Cannot add psevlet.RLS_File module");
	}

	return 0;
}
