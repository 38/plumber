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

#include <typemodel.h>
#include <builtin.h>
#include <const.h>

#include <pservlet.h>

#include <scope/object.h>
#include <scope/string.h>
#include <scope/file.h>

/**
 * @brief How many times did the python module initialized
 **/
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

	/* Let's make the python interpreter able to find what we need */
	PyObject* path = PySys_GetObject("path");
	if(NULL == path)
	    ERROR_LOG_GOTO(ERR, "Cannot get the Python Script search path");
	else if(!PyList_Check(path))
	    ERROR_LOG_GOTO(ERR, "Unexpected type of sys.path, list expected");
	else
	{
		PyObject* servlet_lib_path = PyString_FromString(INSTALL_PREFIX"/lib/plumber/python");

		if(servlet_lib_path == NULL)
		    ERROR_LOG_GOTO(ERR, "Cannot create new string for the servlet lib path");

		if(0 != PyList_Append(path, servlet_lib_path))
		{
			Py_DECREF(servlet_lib_path);
			ERROR_LOG_GOTO(ERR, "Cannot append the additional library path to the Python library path search dir");
		}
	}

	if(NULL == (_module = builtin_init_module()))
	    ERROR_LOG_GOTO(ERR, "Cannot initialize the servlet API module");

	if(ERROR_CODE(int) == const_init(_module))
	    ERROR_LOG_GOTO(ERR, "Cannot initailize the constant");

	if(ERROR_CODE(int) == typemodel_object_init(_module))
	    ERROR_LOG_GOTO(ERR, "Cannot initialize the typemodel object");

	if(ERROR_CODE(int) == scope_object_init(_module))
	    ERROR_LOG_GOTO(ERR, "Cannot intialize the ScopeToken");

	if(ERROR_CODE(int) == scope_string_init(_module))
	    ERROR_LOG_GOTO(ERR, "Cannot initialize the RLS string object");

	if(ERROR_CODE(int) == scope_file_init(_module))
	    ERROR_LOG_GOTO(ERR, "Cannot initialize the RLS file object");

	return 0;

ERR:
	Py_DECREF(_module);
	PyErr_Print();
	Py_Finalize();
	_init_count = 0;
	return ERROR_CODE(int);
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

	PyGILState_STATE state = PyGILState_Ensure();
	Py_XDECREF(s->data);
	Py_XDECREF(s->module);
	PyGILState_Release(state);

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
