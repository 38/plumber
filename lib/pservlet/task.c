/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
task_id_t task_id()
{
	return RUNTIME_ADDRESS_TABLE_SYM->get_tid();
}
