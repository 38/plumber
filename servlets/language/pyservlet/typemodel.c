/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>

#include <predict.h>

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

	if(self->instance != NULL) return 0;

	_type_model_t* py_model;
	if(!PyArg_ParseTuple(args, "O", (PyObject**)&py_model) || py_model->magic != _TM_MAGIC)
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return -1;
	}

	if(NULL == (self->instance = pstd_type_instance_new(py_model->model, NULL)))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return -1;
	}

	return 0;
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
	long    pipe;
	const char* member;
	if(!PyArg_ParseTuple(args, "ls", &pipe, &member))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return NULL;
	}

	pstd_type_accessor_t acc;
	if(ERROR_CODE(pstd_type_accessor_t) == (acc = pstd_type_model_get_accessor(self->model, (pipe_t)pipe, member)))
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot create accessor");
		return NULL;
	}

	return Py_BuildValue("l", (long)acc);
}

static PyObject* _type_instance_read_int(PyObject* _self, PyObject* args)
{
	_type_instance_t* self = (_type_instance_t*)_self;

	long acc;
	int size;
	int is_signed;

	if(!PyArg_ParseTuple(args, "lii", &acc, &size, &is_signed))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return NULL;
	}

	if(size != 1 && size != 2 && size != 4 && size != 8)
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid size");
		return NULL;
	}


	PREDICT_IMPOSSIBLE(size < 1);
	PREDICT_IMPOSSIBLE(size > 8);
	char buf[size];

	union {
		void*    generic;
		uint8_t* u8;
		int8_t*  i8;
		uint16_t* u16;
		int16_t*  i16;
		uint32_t* u32;
		int32_t*  i32;
		uint64_t* u64;
		int64_t*  i64;
	} typed_buf = {
		.generic = buf
	};
	size_t rc = pstd_type_instance_read(self->instance, (pstd_type_accessor_t)acc, buf, (size_t)size);
	if((size_t)size != rc)
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot read the expected size from the data primitive");
		return NULL;
	}

	long retval;
	switch(size)
	{
		case 1:
			if(is_signed) retval = *typed_buf.i8;
			else retval = *typed_buf.u8;
			break;
		case 2:
			if(is_signed) retval = *typed_buf.i16;
			else retval = *typed_buf.u16;
			break;
		case 4:
			if(is_signed) retval = *typed_buf.i32;
			else retval = (long)*typed_buf.u32;
			break;
		case 8:
			if(is_signed) retval = (long)*typed_buf.i64;
			else
				return Py_BuildValue("K", *typed_buf.u64);
			break;
		default:
		    PyErr_SetString(PyExc_RuntimeError, "Code bug!");
		    return NULL;
	}

	return Py_BuildValue("l", retval);
}

static PyObject* _type_instance_read_float(PyObject* _self, PyObject* args)
{
	_type_instance_t* self = (_type_instance_t*)_self;

	long acc;
	int size;

	if(!PyArg_ParseTuple(args, "li", &acc, &size))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return NULL;
	}

	if(size != 4 && size != 8)
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid size");
		return NULL;
	}

	PREDICT_IMPOSSIBLE(size > 8);
	PREDICT_IMPOSSIBLE(size < 4);
	char buf[size];

	union {
		void* generic;
		float* f;
		double* d;
	} typed = {
		.generic = buf
	};
	size_t rc = pstd_type_instance_read(self->instance, (pstd_type_accessor_t)acc, buf, (size_t)size);
	if((size_t)size != rc)
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot read the expected size from the data primitive");
		return NULL;
	}

	double retval;
	switch(size)
	{
		case 4:
			retval = *typed.f;
			break;
		case 8:
			retval = *typed.d;
			break;
		default:
		    PyErr_SetString(PyExc_RuntimeError, "Code bug!");
		    return NULL;
	}

	return Py_BuildValue("d", retval);
}

static PyObject* _type_instance_write_int(PyObject* _self, PyObject* args)
{
	_type_instance_t* self = (_type_instance_t*)_self;

	long acc;
	int size;
	int is_signed;
	long value;

	if(!PyArg_ParseTuple(args, "liil", &acc, &size, &is_signed, &value))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return NULL;
	}

	if(size != 1 && size != 2 && size != 4 && size != 8)
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid size");
		return NULL;
	}

	if(ERROR_CODE(int) == pstd_type_instance_write(self->instance, (pstd_type_accessor_t)acc, &value, (size_t)size))
	{
		PyErr_SetString(PyExc_RuntimeError, "Typed header write error");
		return NULL;
	}

	Py_RETURN_NONE;
}

static PyObject* _type_instance_write_float(PyObject* _self, PyObject* args)
{
	_type_instance_t* self = (_type_instance_t*)_self;

	long acc;
	int size;
	double value;

	if(!PyArg_ParseTuple(args, "lid", &acc, &size, &value))
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid arguments");
		return NULL;
	}

	if(size != 4 && size != 8)
	{
		PyErr_SetString(PyExc_RuntimeError, "Invalid size");
		return NULL;
	}

	float fval = (float)value;
	void* buf = &value;

	if(size == 4) buf = &fval;

	if(ERROR_CODE(int) == pstd_type_instance_write(self->instance, (pstd_type_accessor_t)acc, buf, (size_t)size))
	{
		PyErr_SetString(PyExc_RuntimeError, "Typed header write error");
		return NULL;
	}

	Py_RETURN_NONE;
}

static PyMethodDef _model_methods[] = {
	{"accessor",   _type_model_get_accessor, METH_VARARGS, "Get the accessor for the type model"},
	{NULL,         NULL,                     0,             NULL}
};

static PyMethodDef _inst_methods[] = {
	{"read_int",   _type_instance_read_int,  METH_VARARGS, "Read an integer from the type instance"},
	{"read_float", _type_instance_read_float, METH_VARARGS, "Read a float number from the type instance"},
	{"write_int",  _type_instance_write_int, METH_VARARGS, "Write an integer to the type instance"},
	{"write_float", _type_instance_write_float, METH_VARARGS, "Write an float number to the type instance"},
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
	.tp_methods    = _model_methods
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
	.tp_init       = _type_instance_init,
	.tp_methods    = _inst_methods
};

int typemodel_object_init(PyObject* module)
{
	if(NULL == module)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	union {
		PyTypeObject* tp;
		PyObject*     obj;
	} cvt = {
		.tp = &_py_type_model
	};

	if(PyType_Ready(cvt.tp) == -1)
		ERROR_RETURN_LOG(int, "Cannot initialize the type object");

	Py_INCREF(cvt.obj);

	if(PyModule_AddObject(module, "TypeModel", cvt.obj) == -1)
		ERROR_RETURN_LOG(int, "Cannot add type to module");

	cvt.tp = &_py_type_instance;
	if(PyType_Ready(cvt.tp) == -1)
		ERROR_RETURN_LOG(int, "Cannot intialize the type object");

	Py_INCREF(cvt.obj);

	if(PyModule_AddObject(module, "TypeInstance", cvt.obj) == -1)
		ERROR_RETURN_LOG(int, "Cannot add type to module");

	return 0;
}
