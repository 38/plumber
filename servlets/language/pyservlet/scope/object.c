/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>

#include <pstd.h>
#include <pservlet.h>

#include <scope/object.h>

#define _MAGIC 0x5f3e65a1u

/**
 * @brief The scope object type count
 **/
static scope_object_ops_t _obj_ops[SCOPE_OBJECT_TYPE_COUNT] = {};

/**
 * @brief The python object that holds a scope object
 **/
typedef struct {
	PyObject_HEAD
	uint32_t             magic;  /*!< The magic number for this type */
	scope_token_t        token;  /*!< The RLS token assigned to this object, for the uncommittd object this field should be ERROR_CODE */
	scope_object_type_t  type;   /*!< The type code for this RLS object */
	void*                owned;   /*!< The actual object pointer */
	const void*          in_scope; /*!< The in scope object */
} _py_object_t;

static PyObject* _py_object_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
	(void)args;
	(void)kwds;
	_py_object_t* self;
	if(NULL != (self = (_py_object_t*)type->tp_alloc(type, 0)))
	{
		self->magic = _MAGIC;
		self->type  = SCOPE_OBJECT_TYPE_COUNT;
		self->token = ERROR_CODE(scope_token_t);
		self->owned = NULL;
		self->in_scope = NULL;
	}
	else
	{
		PyErr_SetString(PyExc_RuntimeError, "Cannot create Python RLS Scope Object");
		return NULL;
	}

	return (PyObject*)self;
}

static int _py_object_init(PyObject* _self, PyObject* args, PyObject* kwds)
{
	(void)kwds;
	_py_object_t* self = (_py_object_t*)_self;
	if(self->magic != _MAGIC)
	{
		PyErr_SetString(PyExc_TypeError, "Invalid type magic number");
		return -1;
	}

	long l_type;
	long scope_token;
	PyObject* first = PyTuple_GetSlice(args, 0, 2);
	if(NULL == first || !PyArg_ParseTuple(first, "ll", &l_type, &scope_token))
	{
		Py_DECREF(first);
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return -1;
	}
	Py_DECREF(first);

	if(l_type < 0 || l_type >= SCOPE_OBJECT_TYPE_COUNT)
	{
		PyErr_SetString(PyExc_TypeError, "Invalid scope type");
		return -1;
	}

	scope_object_type_t type = (scope_object_type_t)l_type;

	if(scope_token < 0)
	{
		Py_ssize_t size = PyTuple_Size(args);
		PyObject* remaining = PyTuple_GetSlice(args, 2, size);
		if(NULL == remaining)
		{
			PyErr_SetString(PyExc_RuntimeError, "Cannot create the RLS object creation parameter");
			return -1;
		}

		if(_obj_ops[type].create == NULL || NULL == (self->owned = _obj_ops[type].create(remaining)))
		{
			PyErr_SetString(PyExc_RuntimeError, "Cannot create the RLS object");
			Py_DECREF(remaining);
			return -1;
		}

		Py_DECREF(remaining);

		self->token = ERROR_CODE(scope_token_t);
		self->type = type;
	}
	else
	{
		if(NULL == (self->in_scope = pstd_scope_get((scope_token_t)scope_token)))
		{
			PyErr_SetString(PyExc_RuntimeError, "Cannot retrieve scope token from the RLS scope");
			return -1;
		}

		self->type = (scope_object_type_t)type;
		self->token = (scope_token_t)scope_token;
	}

	return 0;
}

static void _py_object_free(_py_object_t* obj)
{
	if(obj->owned != NULL && obj->type < SCOPE_OBJECT_TYPE_COUNT)
	{
		/* If this scope object haven't been committed, it should be disposed right now */
		const scope_object_ops_t* ops = _obj_ops + obj->type;
		if(NULL != ops->dispose && ERROR_CODE(int) == ops->dispose(obj->owned))
		{
			PyErr_SetString(PyExc_RuntimeError, "Cannot dispose the RLS scope object");
			return;
		}
	}

	Py_TYPE(obj)->tp_free((PyObject*)obj);
}

static PyObject* _py_object_str(PyObject* _self)
{
	_py_object_t* self = (_py_object_t*)_self;
	if(self->magic != _MAGIC || self->type >= SCOPE_OBJECT_TYPE_COUNT)
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	return PyString_FromFormat("<RLS Scope Object %s: %u(%p)>",
	                           _obj_ops[self->type].name, self->token,
	                           (self->token == ERROR_CODE(scope_token_t)) ? self->owned : self->in_scope);
}

static PyObject* _py_object_get_token(PyObject* _self, PyObject* args)
{
	(void)args;
	_py_object_t* self = (_py_object_t*)_self;
	if(self->magic != _MAGIC || self->type >= SCOPE_OBJECT_TYPE_COUNT)
	{
		PyErr_SetString(PyExc_TypeError, "Self pointer is not a RLS scope object");
		return NULL;
	}

	scope_token_t ret = self->token;
	if(ERROR_CODE(scope_token_t) == ret)
	{
		if(_obj_ops[self->type].commit == NULL || (ret = self->token = _obj_ops[self->type].commit(self->owned)) == ERROR_CODE(scope_token_t))
		{
			PyErr_SetString(PyExc_RuntimeError, "Cannot commit the RLS object to the scope");
			return NULL;
		}
		self->in_scope = self->owned;
		self->owned = NULL;
	}

	return Py_BuildValue("l", (long)ret);
}

static PyMethodDef _methods[] = {
	{"get_token", _py_object_get_token, METH_VARARGS, "Get the RLS scope for this object, if object is the uncommitted, commit the object to the RLS" },
	{}
};

/**
 * @brief The type object for the python scope object
 **/
static PyTypeObject _py_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name       = "pyservlet.ScopeObject",
	.tp_flags      = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	.tp_basicsize  = sizeof(_py_object_t),
	.tp_doc        = "The RLS scope token object",
	.tp_new        = _py_object_new,
	.tp_init       = _py_object_init,
	.tp_dealloc    = (destructor)_py_object_free,
	.tp_str        = _py_object_str,
	.tp_methods    = _methods
};

int scope_object_init(PyObject* module)
{
	if(NULL == module)
		ERROR_RETURN_LOG(int, "Invalid arguments");
	if(PyType_Ready(&_py_type) == -1)
		ERROR_RETURN_LOG(int, "Cannot initialize the python object");

	union {
		PyObject*    obj;
		PyTypeObject* tp;
	} cvt = {
		.tp = &_py_type
	};

	Py_INCREF(cvt.obj);

	if(PyModule_AddObject(module, "RLS_Object",  cvt.obj) == -1)
		ERROR_RETURN_LOG(int, "Caonnot add scope token type to module");

	if(PyModule_AddIntConstant(module, "SCOPE_TYPE_STRING", SCOPE_OBJECT_TYPE_STRING) == -1)
		ERROR_RETURN_LOG(int, "Cannot add SCOPE_OBJECT_TYPE_STRING to the module");

	if(PyModule_AddIntConstant(module, "SCOPE_TYPE_FILE", SCOPE_OBJECT_TYPE_FILE) == -1)
		ERROR_RETURN_LOG(int, "Cannot add SCOPE_TYPE_FILE to the module");

	return 0;
}

int scope_object_register_type_ops(scope_object_type_t type, scope_object_ops_t ops)
{
	if(type >= SCOPE_OBJECT_TYPE_COUNT || ops.create == NULL || ops.dispose == NULL || ops.commit == NULL)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_obj_ops[type] = ops;

	return 0;
}

const void* scope_object_retrieve(scope_object_type_t type, PyObject* object)
{
	_py_object_t* obj = (_py_object_t*)object;
	if(type >= SCOPE_OBJECT_TYPE_COUNT || NULL == object || _MAGIC != obj->magic)
	{
		PyErr_SetString(PyExc_TypeError, "Invalid arguments");
		return NULL;
	}

	if(obj->type != type)
	{
		PyErr_SetString(PyExc_TypeError, "Unexpected type code");
		return NULL;
	}

	if(ERROR_CODE(scope_token_t) == obj->token)
		return obj->owned;

	return obj->in_scope;
}
