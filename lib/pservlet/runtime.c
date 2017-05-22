/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>

const char* runtime_version()
{
	return RUNTIME_ADDRESS_TABLE_SYM->version != NULL ? RUNTIME_ADDRESS_TABLE_SYM->version() : "Undefined";
}

