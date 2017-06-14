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
	if(ERROR_CODE(int) == pss_dict_init())
		ERROR_LOG_GOTO(ERR, "Cannot fianlize the dictionary type callbacks");
	return 0;
ERR:
	return ERROR_CODE(int);
}

int pss_finalize()
{
	int rc = 0;
	if(ERROR_CODE(int) == pss_dict_finalize())
	{
		LOG_ERROR("Cannot finalize the dictionary type");
		rc = ERROR_CODE(int);
	}
	if(ERROR_CODE(int) == pss_closure_init())
	{
		LOG_ERROR("Cannot finalize the closure type");
		rc = ERROR_CODE(int);
	}
	return rc;
}
