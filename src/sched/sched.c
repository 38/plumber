/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <utils/init.h>
#include <api.h>
#include <itc/itc.h>
#include <runtime/runtime.h>
#include <sched/sched.h>

INIT_VEC(modules) = {
	INIT_MODULE(sched_task),
	INIT_MODULE(sched_loop),
	INIT_MODULE(sched_prof),
	INIT_MODULE(sched_rscope),
	INIT_MODULE(sched_async)
};

int sched_init()
{
	return INIT_DO_INITIALIZATION(modules);
}

int sched_finalize()
{
	return INIT_DO_FINALIZATION(modules);
}
