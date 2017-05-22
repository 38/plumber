/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>

void trap(int id)
{
	if(RUNTIME_ADDRESS_TABLE_SYM->trap != NULL) RUNTIME_ADDRESS_TABLE_SYM->trap(id);
}

