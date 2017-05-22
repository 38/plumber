/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <pservlet.h>

pipe_t module_require_function(const char* mod_name, const char* func)
{
	return RUNTIME_ADDRESS_TABLE_SYM->get_module_func(mod_name, func);
}

uint8_t module_open(const char* path)
{
	return RUNTIME_ADDRESS_TABLE_SYM->mod_open(path);
}
