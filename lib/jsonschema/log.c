/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>

#include <jsonschema/log.h>
#include <error.h>

static jsonschema_log_write_func_t _log_write;

int jsonschema_log_set_write_callback(jsonschema_log_write_func_t func)
{
	if(func == NULL)
		return ERROR_CODE(int);
	_log_write = func;
	return 0;
}


void jsonschema_log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
{
	if(_log_write == NULL) return;
	va_list ap;
	va_start(ap, fmt);
	_log_write(level, file, function, line, fmt, ap);
	va_end(ap);
}
