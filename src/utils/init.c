/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <utils/init.h>
#include <error.h>

int init_do_initialization(size_t sz, init_vec_entry_t* init_vec)
{
	size_t stage;
	for(stage = 0; stage < sz; stage ++)
		if(init_vec[stage].init() < 0)
			goto ERR;
	return 0;
ERR:
	for(; stage > 0; init_vec[--stage].finalize());
	return ERROR_CODE(int);
}

int init_do_finalization(size_t sz, init_vec_entry_t* init_vec)
{
	int rc = 0;
	for(; sz > 0;)
		if(init_vec[--sz].finalize() < 0)
			rc = ERROR_CODE(int);
	return rc;
}
