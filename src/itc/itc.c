/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <api.h>
#include <itc/itc.h>
#include <utils/init.h>

INIT_VEC(modules) = {
	INIT_MODULE(itc_modtab),
	INIT_MODULE(itc_module),
	INIT_MODULE(itc_equeue),
	INIT_MODULE(itc_eloop)
};

int itc_init()
{
	return INIT_DO_INITIALIZATION(modules);
}

int itc_finalize()
{
	return INIT_DO_FINALIZATION(modules);
}
