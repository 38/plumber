/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <stdarg.h>

void log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	RUNTIME_ADDRESS_TABLE_SYM->log_write(level, file, function, line, fmt, ap);

	va_end(ap);
}
