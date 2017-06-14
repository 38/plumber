/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <error.h>
#include <pss.h>

int pss_init()
{
	if(ERROR_CODE(int) == pss_closure_init())
		ERROR_LOG_GOTO(ERR, "Cannot initialize the closure type callbacks");
	return 0;
ERR:
	return ERROR_CODE(int);
}

int pss_finalize()
{
	return 0;
}
