/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <utils/utils.h>
#include <utils/log.h>
#include <utils/mempool/page.h>
#include <utils/init.h>

INIT_VEC(modules) = {
	INIT_MODULE(log),
	INIT_MODULE(mempool_page)
};

int utils_init()
{
	return INIT_DO_INITIALIZATION(modules);
}

int utils_finalize()
{
	return INIT_DO_FINALIZATION(modules);
}
