/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <runtime/api.h>
#include <itc/itc.h>
#include <runtime/runtime.h>
#include <utils/init.h>
INIT_VEC(modules) = {
	INIT_MODULE(runtime_stab),
	INIT_MODULE(runtime_servlet),
	INIT_MODULE(runtime_task)
};

int runtime_init()
{
	return INIT_DO_INITIALIZATION(modules);
}

int runtime_finalize()
{
	return INIT_DO_FINALIZATION(modules);
}
