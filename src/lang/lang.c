/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <plumber.h>
#include <utils/init.h>

INIT_VEC(modules) = {
	INIT_MODULE(lang_prop)
};


int lang_init()
{
	return INIT_DO_INITIALIZATION(modules);
}

int lang_finalize()
{
	return INIT_DO_FINALIZATION(modules);
}
