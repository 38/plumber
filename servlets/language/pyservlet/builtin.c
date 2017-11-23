/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>
#include <pservlet.h>
#include <builtin.h>

static PyObject* _pyservlet_define(PyObject* self, PyObject *args)
{
	(void) self;
	const char* name;
	const char* type_expr = NULL;
	int flags;
	if(!PyArg_ParseTuple(args, "si|s", &name, &flags, &type_expr))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	pipe_t rc = pipe_define(name, (runtime_api_pipe_flags_t)flags, type_expr);

	if(rc == ERROR_CODE(pipe_t))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot define a pipe");
		return NULL;
	}
	return Py_BuildValue("l", (long)rc);
}

static PyObject* _pyservlet_read(PyObject* self, PyObject* args)
{
	(void) self;
	long pipe;
	int howmany = -1;
	if(!PyArg_ParseTuple(args, "l|i", &pipe, &howmany))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	size_t count = (howmany >= 0) ? (size_t)howmany : (size_t)-1;
	char buffer[4096];

	PyObject* result = NULL, *current_part = NULL;

	for(;count > 0;)
	{
		size_t bytes_to_read = sizeof(buffer);
		if(bytes_to_read > count) bytes_to_read = count;

		size_t bytes_read = pipe_read((pipe_t)pipe, buffer, bytes_to_read);

		if(bytes_read == ERROR_CODE(size_t)) goto ERR;
		if(bytes_read == 0) break;

		if(NULL == (current_part = PyString_FromStringAndSize(buffer, (Py_ssize_t)bytes_read)))
		    ERROR_LOG_GOTO(ERR, "Cannot convert the read data to python object");

		if(NULL == result)
		    result = current_part;
		else
		{
			PyString_Concat(&result, current_part);
			if(NULL == result) ERROR_LOG_GOTO(ERR, "Cannot concatentate the result");
			Py_XDECREF(current_part);
			current_part = NULL;
		}

		current_part = NULL;
		count -= bytes_read;
	}

	if(result == NULL) result = PyString_FromString("");
	return result;
ERR:
	Py_XDECREF(current_part);
	Py_XDECREF(result);
	PyErr_SetString(PyExc_IOError, "Read failure, see Plumber log for details");
	return NULL;
}

static PyObject* _pyservlet_write(PyObject* self, PyObject* args)
{
	(void) self;
	long pipe;
	PyObject* dataObject;
	Py_ssize_t size;
	char* buffer;

	if(!PyArg_ParseTuple(args, "lS", &pipe, &dataObject)) //ERROR_PTR_RETURN_LOG("Invalid arguments");
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	if(PyString_AsStringAndSize(dataObject, &buffer, &size) == -1)// ERROR_PTR_RETURN_LOG("Cannot get the string content");
	{
		PyErr_SetString(PyExc_TypeError, "Cannot get the string to read");
		return NULL;
	}

	size_t rc = pipe_write((pipe_t)pipe, buffer, (size_t)size);
	if(rc == ERROR_CODE(size_t))
	{
		PyErr_SetString(PyExc_IOError, "Write failure, see Plumber log for details");
		return NULL;
	}
	return Py_BuildValue("k", (unsigned long)rc);
}

static PyObject* _pyservlet_log(PyObject* self, PyObject* args)
{
	(void) self;
	int level;
	const char* message;
	if(!PyArg_ParseTuple(args, "is", &level, &message))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

#define __LOG_LEVEL__(l) else if(l == level) { LOG_##l("%s", message); }
	if(0);
	__LOG_LEVEL__(FATAL)
	__LOG_LEVEL__(ERROR)
	__LOG_LEVEL__(WARNING)
	__LOG_LEVEL__(NOTICE)
	__LOG_LEVEL__(INFO)
	__LOG_LEVEL__(TRACE)
	__LOG_LEVEL__(DEBUG)
#undef __LOG_LEVEL__

	Py_RETURN_NONE;
}

static PyObject* _pyservlet_eof(PyObject* self, PyObject* args)
{
	(void) self;
	long pipe;
	if(!PyArg_ParseTuple(args, "l", &pipe))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	int rc = pipe_eof((pipe_t)pipe);

	if(rc == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot finish the pipe_eof call, see Plumber log for details");
		return NULL;
	}
	else return Py_BuildValue("i", rc);
}

static PyObject* _pyservlet_get_flags(PyObject* self, PyObject* args)
{
	(void)self;
	long pipe;
	pipe_flags_t flags;
	if(!PyArg_ParseTuple(args, "l", &pipe))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	if(pipe_cntl((pipe_t)pipe, PIPE_CNTL_GET_FLAGS, &flags) == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete the pipe_cntl call, see Plumber log for details");
		return NULL;
	}
	return Py_BuildValue("l", (long)flags);
}

static PyObject* _pyservlet_set_flag(PyObject* self, PyObject* args)
{
	(void)self;
	long pipe;
	long flags;
	if(!PyArg_ParseTuple(args, "ll", &pipe, &flags))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	if(pipe_cntl((pipe_t)pipe, PIPE_CNTL_SET_FLAG, (pipe_flags_t)flags) == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete the pipe_cntl call, see Plumber log for details");
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject* _pyservlet_clr_flag(PyObject* self, PyObject* args)
{
	(void)self;
	long pipe;
	long flags;
	if(!PyArg_ParseTuple(args, "ll", &pipe, &flags))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	if(pipe_cntl((pipe_t)pipe, PIPE_CNTL_CLR_FLAG, (pipe_flags_t)flags) == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete pipe_cntl call, see Plumber log for details");
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject* _pyservlet_version(PyObject* self, PyObject* args)
{
	(void)self;
	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	const char* ptr = runtime_version();

	return Py_BuildValue("s", ptr);
}


static int _pyobject_free(void* obj)
{
	if(NULL == obj) ERROR_RETURN_LOG(int, "Invalid arguments");

	PyGILState_STATE state = PyGILState_Ensure();
	PyObject* pyobj = (PyObject*)obj;
	Py_DECREF(pyobj);
	PyGILState_Release(state);
	return 0;
}

static PyObject* _pyservlet_push_state(PyObject* self, PyObject* args)
{
	(void)self;
	long pipe;
	PyObject* state;
	if(!PyArg_ParseTuple(args, "lO", &pipe, &state))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	if(ERROR_CODE(int) == pipe_cntl((pipe_t)pipe, PIPE_CNTL_PUSH_STATE, state, _pyobject_free))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete pipe_cntl call, see Plumber log for details");
		return NULL;
	}

	Py_XINCREF(state);

	Py_RETURN_NONE;
}

static PyObject* _pyservlet_pop_state(PyObject* self, PyObject* args)
{
	(void)self;
	long pipe;
	PyObject* state;
	if(!PyArg_ParseTuple(args, "l", &pipe))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	if(ERROR_CODE(int) == pipe_cntl((pipe_t)pipe, PIPE_CNTL_POP_STATE, &state))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete pipe_cntl call, see Plumber log for details");
		return NULL;
	}

	if(state == NULL) Py_RETURN_NONE;

	Py_XINCREF(state);
	return state;
}

static PyMethodDef methods[] = {
	/* Pipe manipulation */
	{"pipe_define",    _pyservlet_define,     METH_VARARGS,     "Define a named pipe"},
	{"pipe_read",      _pyservlet_read,       METH_VARARGS,     "Read data from pipe"},
	{"pipe_write",     _pyservlet_write,      METH_VARARGS,     "Write data from pipe"},
	{"pipe_eof",       _pyservlet_eof,        METH_VARARGS,     "Check if the pipe has no more data"},
	{"pipe_get_flags", _pyservlet_get_flags,  METH_VARARGS,     "Get the flags of the pipe"},
	{"pipe_set_flag",  _pyservlet_set_flag,   METH_VARARGS,     "Set the flags of the pipe"},
	{"pipe_clr_flag",  _pyservlet_clr_flag,   METH_VARARGS,     "Clear a flag bit"},
	{"pipe_push_state",_pyservlet_push_state, METH_VARARGS,     "Push a state to the pipe"},
	{"pipe_pop_state", _pyservlet_pop_state,  METH_VARARGS,     "Pop the previously pushed state"},
	/* Log utils */
	{"log",            _pyservlet_log,        METH_VARARGS,     "Write a log to plumber logging system"},
	/* Version */
	{"plumber_version",_pyservlet_version,   METH_VARARGS,     "Get the version code of plumber"},
	{NULL,             NULL,                  0,                NULL}
};

PyObject* builtin_init_module()
{
	return Py_InitModule("pservlet", methods);
}
