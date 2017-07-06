/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>

#include <pservlet.h>
#include <pstd.h>

#include <typemodel.h>

#define _TM_MAGIC 0x32fed42fu

#define _TI_MAGIC 0x5f245fabu

/**
 * @brief The internal data structure for a type model type
 **/
typedef struct {
	PyObject_HEAD
	uint32_t           magic;   /*!< The magic number used to identify this type */
	pstd_type_model_t* model;   /*!< The type module */
} _type_model_t;

/**
 * @brief The internal data structure for a type instance
 **/
typedef struct {
	PyObject_HEAD
	uint32_t              magic;      /*!< The type id */
	pstd_type_instance_t* instance;   /*!< The type instance */
} _type_instance_t;

static PyObject* _type_model_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
	(void)args;
	(void)kwds;
	_type_model_t* self;
	if(NULL != (self = (_type_model_t*)type->tp_alloc(type, 0)))
	{
		if(NULL == (self->model = pstd_type_model_new()))
		{
			Py_DECREF(self);
			PyErr_SetString(PyExc_RuntimeError, "Cannot create new type model");
			return NULL;
		}
		self->magic = _TM_MAGIC;
	}
	else PyErr_SetString(PyExc_RuntimeError, "Cannot allocate new python object");

	return (PyObject*)self;
}

static PyObject* _type_instance_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
	(void)args;
	(void)kwds;
	_type_instance_t* self;
	if(NULL != (self = (_type_instance_t*)type->tp_alloc(type, 0)))
	{
		self->instance = NULL;
		self->magic = _TI_MAGIC;
	}
	else PyErr_SetString(PyExc_RuntimeError, "Cannot Allocate new python object for the type instance");

	return (PyObject*)self;
}

static int _type_instance_init(PyObject* _self, PyObject* args, PyObject* kwds)
{
	(void)args;
	(void)kwds;
	_type_instance_t* self = (_type_instance_t*)_self;
	
	if(self->instance != NULL) return 1;

	_type_model_t* py_model;
	if(!PyArg_ParseTuple(args, "o", (PyObject**)&py_model) || py_model->magic != _TM_MAGIC)
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return 0;
	}

	if(NULL == (self->instance = pstd_type_instance_new(py_model->model, NULL)))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return 0;
	}

	return 1;
}

static void _type_model_free(_type_model_t* self)
{
	if(ERROR_CODE(int) == pstd_type_model_free(self->model))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot dispose the tpye model");
		return;
	}
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static void _type_instance_free(_type_instance_t* self)
{
	if(ERROR_CODE(int) == pstd_type_instance_free(self->instance))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot dipsose the type instance");
		return;
	}
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* _type_model_str(PyObject* self)
{
	PyObject* ret = PyString_FromFormat("<pstd type model at %p>", ((_type_model_t*)self)->model);
	return ret;
}

static PyObject* _type_instance_str(PyObject* self)
{
	return PyString_FromFormat("<pstd type instance %p>", ((_type_instance_t*)self)->instance);
}

static PyObject* _type_model_get_accessor(PyObject* _self, PyObject* args)
{
	_type_model_t* self = (_type_model_t*)_self;
	pipe_t    pipe;
	const char* member;
	if(!PyArg_ParseTuple(args, "is", &pipe, &member))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return NULL;
	}

	pstd_type_accessor_t acc;
	if(ERROR_CODE(pstd_type_accessor_t) == (acc = pstd_type_model_get_accessor(self->model, pipe, member)))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot create accessor");
		return NULL;
	}

	return Py_BuildValue("i", acc);	
}

static PyMethodDef _methods[] = {
	{"accessor",   _type_model_get_accessor, METH_VARARGS, "Get the accessor for the type model"},
	{NULL,         NULL,                     0,             NULL}
};


static PyTypeObject _py_type_model= {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name       = "pyservlet.TypeModel",
	.tp_flags      = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	.tp_basicsize  = sizeof(_type_model_t),
	.tp_doc        = "The pipe typed header model",
	.tp_new        = _type_model_new,
	.tp_dealloc    = (destructor)_type_model_free,
	.tp_str        = _type_model_str,
	.tp_methods    = _methods
};

static PyTypeObject _py_type_instance = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name       = "pyservlet.TypeInstance",
	.tp_flags      = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	.tp_basicsize  = sizeof(_type_instance_t),
	.tp_doc        = "The pipe type instance",
	.tp_new        = _type_instance_new,
	.tp_dealloc    = (destructor)_type_instance_free,
	.tp_str        = _type_instance_str,
	.tp_init       = _type_instance_init
};

int typemodel_object_init(PyObject* module)
{
	if(NULL == module) 
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(!PyType_Ready(&_py_type_model))
		ERROR_RETURN_LOG(int, "Cannot initialize the type object");

	Py_INCREF(&_py_type_model);

	if(!PyModule_AddObject(module, "TypeModel", (PyObject*)&_py_type_model))
	{
		Py_DECREF(&_py_type_model);
		ERROR_RETURN_LOG(int, "Cannot add type to module");
	}

	if(!PyType_Ready(&_py_type_instance))
		ERROR_RETURN_LOG(int, "Cannot intialize the type object");

	Py_INCREF(&_py_type_instance);

	if(!PyModule_AddObject(module, "TypeInstance", (PyObject*)&_py_type_instance))
	{
		Py_DECREF(&_py_type_instance);
		ERROR_RETURN_LOG(int, "Cannot add type to module");
	}

	return 0;
}
