/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <pservlet.h>
#include <stdarg.h>
int async_cntl(async_handle_t* handle, uint32_t opcode, ...)
{
	va_list ap;
	va_start(ap, opcode);
	int rc = RUNTIME_ADDRESS_TABLE_SYM->async_cntl(handle, opcode, ap);
	va_end(ap);
	return rc;
}
