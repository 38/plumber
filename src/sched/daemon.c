/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <error.h>
#include <constants.h>

#include <utils/log.h>

#include <lang/prop.h>

#include <sched/daemon.h>

/**
 * @brief The daemon identifier
 **/
static char _id[SCHED_DAEMON_MAX_ID_LEN];

static inline int _set_prop(const char* symbol, lang_prop_value_t value, const void* data)
{
	(void)data;
	if(strcmp(symbol, "id") == 0)
	{
		if(value.type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mismatch");
		snprintf(_id, sizeof(_id), "%s", value.str);
	}
	else return 0;
	return 1;
}

static lang_prop_value_t _get_prop(const char* symbol, const void* param)
{
	(void)param;
	lang_prop_value_t ret = {
		.type = LANG_PROP_TYPE_NONE
	};
	if(strcmp(symbol, "id") == 0)
	{
		ret.type = LANG_PROP_TYPE_STRING;
		
		if(NULL == (ret.str = strdup(_id)))
		{
			LOG_WARNING_ERRNO("Cannot allocate memory for the path string");
			ret.type = LANG_PROP_TYPE_ERROR;
			return ret;
		}
	}

	return ret;
}

int sched_daemon_init()
{
	lang_prop_callback_t cb = {
		.param = NULL,
		.get   = _get_prop,
		.set   = _set_prop,
		.symbol_prefix = "runtime.daemon"
	};

	if(ERROR_CODE(int) == lang_prop_register_callback(&cb))
	    ERROR_RETURN_LOG(int, "Cannot register callback for the runtime prop callback");

	return 0;
}

int sched_daemon_finalize()
{
	return 0;
}
