/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @note because of the GIL, python can not fully take the advantage
 *       of multithreading. Unless we implement a multiprocess model,
 *       it's not recommended use python too much in the service. <br/>
 *       But a single python node is not that blocking, if any other
 *       parts do not waiting for python GIL
 * @file pyservlet/servlet.c
 **/
#include <Python.h>
#include <pservlet.h>
#include <error.h>
/**
 * @brief How many times did the python module initialized */
static int _init_count = 0;

/**
 * @brief the module object
 **/
static PyObject* _module = NULL;

/**
 * @brief The thread data for the main thread
 **/
static PyThreadState* _main_state = NULL;

/**
 * @brief the servlet data
 **/
typedef struct {
	PyObject* module;   /*!< the servlet module */
	PyObject* data;     /*!< the servlet context */
	uint32_t  pipe_count; /*!< the pipe count */
} servlet_data_t;

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
	return Py_BuildValue("i", rc);
}

static PyObject* _pyservlet_read(PyObject* self, PyObject* args)
{
	(void) self;
	pipe_t pipe;
	int howmany = -1;
	if(!PyArg_ParseTuple(args, "i|i", &pipe, &howmany))
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

		size_t bytes_read = pipe_read(pipe, buffer, bytes_to_read);

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

PyObject* _pyservlet_write(PyObject* self, PyObject* args)
{
	(void) self;
	pipe_t pipe;
	PyObject* dataObject;
	Py_ssize_t size;
	char* buffer;

	if(!PyArg_ParseTuple(args, "iS", &pipe, &dataObject)) //ERROR_PTR_RETURN_LOG("Invalid arguments");
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	if(PyString_AsStringAndSize(dataObject, &buffer, &size) == -1)// ERROR_PTR_RETURN_LOG("Cannot get the string content");
	{
		PyErr_SetString(PyExc_TypeError, "Cannot get the string to read");
		return NULL;
	}

	size_t rc = pipe_write(pipe, buffer, (size_t)size);
	if(rc == ERROR_CODE(size_t))
	{
		PyErr_SetString(PyExc_IOError, "Write failure, see Plumber log for details");
		return NULL;
	}
	return Py_BuildValue("i", rc);
}

PyObject* _pyservlet_task_id(PyObject* self, PyObject* args)
{
	(void) self;
	if(!PyArg_ParseTuple(args, ""))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	runtime_api_task_id_t rc = task_id();
	if(ERROR_CODE(runtime_api_task_id_t) == rc)
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete task_id call, see Plumber log for detials");
		return NULL;
	}
	return Py_BuildValue("i", rc);
}

PyObject* _pyservlet_log(PyObject* self, PyObject* args)
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

PyObject* _pyservlet_eof(PyObject* self, PyObject* args)
{
	(void) self;
	pipe_t pipe;
	if(!PyArg_ParseTuple(args, "i", &pipe))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	int rc = pipe_eof(pipe);

	if(rc == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot finish the pipe_eof call, see Plumber log for details");
		return NULL;
	}
	else return Py_BuildValue("i", rc);
}

PyObject* _pyservlet_get_flags(PyObject* self, PyObject* args)
{
	(void)self;
	pipe_t pipe;
	pipe_flags_t flags;
	if(!PyArg_ParseTuple(args, "i", &pipe))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	if(pipe_cntl(pipe, PIPE_CNTL_GET_FLAGS, &flags) == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete the pipe_cntl call, see Plumber log for details");
		return NULL;
	}
	return Py_BuildValue("i", flags);
}

PyObject* _pyservlet_set_flag(PyObject* self, PyObject* args)
{
	(void)self;
	pipe_t pipe;
	pipe_flags_t flags;
	if(!PyArg_ParseTuple(args, "ii", &pipe, &flags))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	if(pipe_cntl(pipe, PIPE_CNTL_SET_FLAG, flags) == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete the pipe_cntl call, see Plumber log for details");
		return NULL;
	}
	Py_RETURN_NONE;
}

PyObject* _pyservlet_clr_flag(PyObject* self, PyObject* args)
{
	(void)self;
	pipe_t pipe;
	pipe_flags_t flags;
	if(!PyArg_ParseTuple(args, "ii", &pipe, &flags))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}
	if(pipe_cntl(pipe, PIPE_CNTL_CLR_FLAG, flags) == ERROR_CODE(int))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete pipe_cntl call, see Plumber log for details");
		return NULL;
	}
	Py_RETURN_NONE;
}

PyObject* _pyservlet_version(PyObject* self, PyObject* args)
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

PyObject* _pyservlet_push_state(PyObject* self, PyObject* args)
{
	(void)self;
	pipe_t pipe;
	PyObject* state;
	if(!PyArg_ParseTuple(args, "iO", &pipe, &state))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_PUSH_STATE, state, _pyobject_free))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete pipe_cntl call, see Plumber log for details");
		return NULL;
	}

	Py_XINCREF(state);

	Py_RETURN_NONE;
}

PyObject* _pyservlet_pop_state(PyObject* self, PyObject* args)
{
	(void)self;
	pipe_t pipe;
	PyObject* state;
	if(!PyArg_ParseTuple(args, "i", &pipe))
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_POP_STATE, &state))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot complete pipe_cntl call, see Plumber log for details");
		return NULL;
	}

	if(state == NULL) Py_RETURN_NONE;

	Py_XINCREF(state);
	return state;
}

/**
 * @brief initialize the Python-Pservlet Interface
 * @return status code
 **/
static inline int _init_ppi()
{
	if(_init_count++ != 0)
	{
		PyEval_RestoreThread(_main_state);
		return 0;
	}

	Py_Initialize();
	PyEval_InitThreads();

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
		/* Task info */
		{"task_id",        _pyservlet_task_id,    METH_VARARGS,     "Get current task ID"},
		/* Log utils */
		{"log",            _pyservlet_log,        METH_VARARGS,     "Write a log to plumber logging system"},
		/* Version */
		{"plumber_version",_pyservlet_clr_flag,   METH_VARARGS,     "Get the version code of plumber"},
		{NULL,             NULL,                  0,                NULL}
	};

	_module = Py_InitModule("pservlet", methods);
	if(NULL != _module)
	{
		PyModule_AddIntConstant(_module, "PIPE_INPUT",     PIPE_INPUT);
		PyModule_AddIntConstant(_module, "PIPE_OUTPUT",    PIPE_OUTPUT);
		PyModule_AddIntConstant(_module, "PIPE_ASYNC",     PIPE_ASYNC);
		PyModule_AddIntConstant(_module, "PIPE_PERSIST",   PIPE_PERSIST);
		PyModule_AddIntConstant(_module, "PIPE_SHADOW",    PIPE_SHADOW);
		PyModule_AddIntConstant(_module, "PIPE_DISABLED",  PIPE_DISABLED);

		PyModule_AddIntConstant(_module, "LOG_FATAL",   FATAL);
		PyModule_AddIntConstant(_module, "LOG_ERROR",   ERROR);
		PyModule_AddIntConstant(_module, "LOG_WARNING", WARNING);
		PyModule_AddIntConstant(_module, "LOG_NOTICE",  NOTICE);
		PyModule_AddIntConstant(_module, "LOG_INFO",    INFO);
		PyModule_AddIntConstant(_module, "LOG_TRACE",   TRACE);
		PyModule_AddIntConstant(_module, "LOG_DEBUG",   DEBUG);
	}

	if(NULL == _module)
	{
		PyErr_Print();
		Py_Finalize();
		_init_count = 0;
		ERROR_RETURN_LOG(int, "Cannot intialize the servlet API module");
	}

	return 0;
}

/**
 * @brief finalize the PPI
 * @return sttus code
 **/
static inline int _finalize_ppi()
{
	if(-- _init_count != 0) return 0;
	PyGILState_Ensure();
	Py_Finalize();

	return 0;
}

int init(uint32_t argc, char const* const* argv, void* data)
{
	int ret = 0;

	if(argc < 2) ERROR_RETURN_LOG(int, "PyServlet expects at least one argument");

	/* Because we may have multiple place that is using this servlet, but we only
	 * needs to initialize python once. */
	if(_init_ppi() == ERROR_CODE(int)) return ERROR_CODE(int);

	servlet_data_t* servlet = (servlet_data_t*)data;
	servlet->data = servlet->module = NULL;

	PyObject* init_func = NULL;
	PyObject* args = NULL;
	PyObject* argstuple = NULL;

	servlet->module = PyImport_ImportModule(argv[1]);
	if(NULL != servlet->module)
	{
		uint32_t i;
		/* Get the Init function */
		init_func = PyObject_GetAttrString(servlet->module, "init");
		if(NULL == init_func) ERROR_LOG_GOTO(PYERR, "Cannot found init function");
		if(!PyCallable_Check(init_func)) ERROR_LOG_GOTO(PYERR, "Initializer is not callable");

		/* Construct the args object */
		args = PyList_New((Py_ssize_t)(argc - 1));
		if(NULL == args) ERROR_LOG_GOTO(PYERR, "Cannot create the argument list");

		for(i = 1; i < argc; i ++)
		{
			PyObject* current = PyString_FromString(argv[i]);
			if(NULL == current) ERROR_LOG_GOTO(PYERR, "Cannot convert current string to python object");
			if(PyList_SetItem(args, (Py_ssize_t)(i - 1), current) >= 0) continue;

			Py_DECREF(current);
			ERROR_LOG_GOTO(PYERR, "Cannot put the argument string to the argument tuple");
		}

		/* Construct the initiazer arguments */
		if(NULL == (argstuple = PyTuple_New(1))) ERROR_LOG_GOTO(PYERR, "Cannot create argument tuple");
		if(PyTuple_SetItem(argstuple, 0, args) < 0) ERROR_LOG_GOTO(PYERR, "Cannot set argument list");
		args = NULL;  /* The reference has been stolen */

		if(NULL == (servlet->data = PyObject_CallObject(init_func, argstuple)))
		    ERROR_LOG_GOTO(PYERR, "Cannot call exec function of servlet %s", argv[1]);
	}
	else ERROR_LOG_GOTO(PYERR, "Cannot find module %s", argv[1]);

	goto PYNORMAL;

PYERR:
	ret = ERROR_CODE(int);
	PyErr_Print();
	Py_XDECREF(servlet->module);
	Py_XDECREF(servlet->data);
PYNORMAL:
	Py_XDECREF(argstuple);
	Py_XDECREF(init_func);

	if(ret == ERROR_CODE(int)) _finalize_ppi();
	else _main_state = PyEval_SaveThread();
	return ret;
}

/**
 * @brief invoke a servlet function, either exec or cleanup
 * @param s the servlet context
 * @param name the name of the function
 **/
static inline int _invoke_servlet_function(servlet_data_t* s, const char* name)
{
	if(NULL == s->module) ERROR_RETURN_LOG(int, "Invalid arguments");
	int rc = -1;
	if(NULL != s->module)
	{
		PyGILState_STATE state = PyGILState_Ensure();
		PyObject* args = NULL;
		PyObject* result = NULL;

		/* Get the function */
		PyObject* func = PyObject_GetAttrString(s->module, name);
		if(NULL == func) goto PYEXIT;
		if(PyCallable_Check(func) < 0) ERROR_LOG_GOTO(PYEXIT, "Attribute %s is not callable", name);

		/* Construct arguments */
		args = PyTuple_New(1);
		if(NULL == args) ERROR_LOG_GOTO(PYERR, "Cannot create argument tuple");

		Py_INCREF(s->data);  /* Reference will be stolen, so incref first */
		if(PyTuple_SetItem(args, 0, s->data) < 0)
		{
			Py_DECREF(s->data);
			ERROR_LOG_GOTO(PYERR, "Cannot set argument tuple");
		}

		/* Call the function ! */
		result = PyObject_CallObject(func, args);
		if(NULL == result) ERROR_LOG_GOTO(PYERR, "Function %s do not return normally", name);

		/* Check the result */
		if(!PyInt_Check(result))
		    ERROR_LOG_GOTO(PYERR, "Integer return value expected from function %s", name);

		PyErr_Clear();
		rc = (int)PyInt_AsLong(result);
		if(PyErr_Occurred()) ERROR_LOG_GOTO(PYERR, "Cannot convert status code to native value");

		goto PYEXIT;
PYERR:
		rc = ERROR_CODE(int);
		PyErr_Print();
PYEXIT:
		Py_XDECREF(func);
		Py_XDECREF(args);
		Py_XDECREF(result);
		PyGILState_Release(state);
	}

	return rc;
}

int exec(void* data)
{
	return _invoke_servlet_function((servlet_data_t*)data, "execute");
}

int cleanup(void* data)
{
	int rc = 0;
	servlet_data_t* s = (servlet_data_t*)data;
	rc = _invoke_servlet_function(s, "unload");

	Py_XDECREF(s->data);
	Py_XDECREF(s->module);

	_finalize_ppi();
	return rc;
}

SERVLET_DEF = {
	.desc = "Python Servlet Loader",
	.version = 0x0,
	.size = sizeof(servlet_data_t),
	.init = init,
	.exec = exec,
	.unload = cleanup
};
