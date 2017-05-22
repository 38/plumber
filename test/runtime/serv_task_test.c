/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <pservlet.h>
#include <stdlib.h>
int init(uint32_t argc, char const* const* argv, void* data)
{
	(void) argc;
	(void) argv;
	(void) data;
	trap(0);
	return 0;
}
int exec(void* data)
{
	(void) data;
	trap(1);
	return 0;
}
int unload(void* data)
{
	(void) data;
	trap(2);
	return 0;
}
SERVLET_DEF = {
	.size = 0,
	.version = 0,
	.desc = "Task Test Helper",
	.init = init,
	.exec = exec,
	.unload = unload
};

