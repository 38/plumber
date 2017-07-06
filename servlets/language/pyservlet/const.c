/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <Python.h>
#include <pservlet.h>

#include <const.h>

typedef struct {
	const char* name;
	long        value;
} _const_t;

#define _EXPORT_CONST(v) { .name = #v, .value = v }
#define _EXPORT_NAMED_CONST(n, v) { .name = n, .value = v }

static _const_t _const[] = {
	_EXPORT_CONST(PIPE_INPUT),
	_EXPORT_CONST(PIPE_OUTPUT),
	_EXPORT_CONST(PIPE_ASYNC),
	_EXPORT_CONST(PIPE_PERSIST),
	_EXPORT_CONST(PIPE_SHADOW),
	_EXPORT_CONST(PIPE_DISABLED),
	_EXPORT_NAMED_CONST("LOG_FATAL", FATAL),
	_EXPORT_NAMED_CONST("LOG_ERROR", ERROR),
	_EXPORT_NAMED_CONST("LOG_WARNING", WARNING),
	_EXPORT_NAMED_CONST("LOG_NOTICE", NOTICE),
	_EXPORT_NAMED_CONST("LOG_INFO", INFO),
	_EXPORT_NAMED_CONST("LOG_TRACE", TRACE),
	_EXPORT_NAMED_CONST("LOG_DEBUG", DEBUG)
};

int const_init(PyObject* module)
{
	if(NULL == module) ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t i;
	for(i = 0; i < sizeof(_const) / sizeof(_const[0]); i ++)
		if(PyModule_AddIntConstant(module, _const[i].name, _const[i].value) < 0)
			ERROR_RETURN_LOG(int, "Cannot register constant %s", _const[i].name);
	return 0;
}
