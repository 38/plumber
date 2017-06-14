/**
 * Copyright (C) 2017, Hao Hou
 * Copyright (C) 2017, Feng Liu
 **/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>

static pss_value_ref_ops_t _type_ops[PSS_VALUE_REF_TYPE_COUNT];

static const pss_value_t _EVALUE = { .kind = PSS_VALUE_KIND_ERROR };

struct _pss_value_ref_t {
	pss_value_ref_type_t type;
	void* p_val;
	uint32_t ref_count;
};

#define _ERROR_RETURN(return_val, msg, arg...) \
	do { \
		LOG_ERROR(msg, ##arg); \
		return return_val; \
	} while(0)

#define _VALIDATE_TYPE(type, return_val, msg, arg...) \
	if(type < 0 || type >= PSS_VALUE_REF_TYPE_COUNT) \
	{ \
		_ERROR_RETURN(return_val, msg, ##arg); \
	}

#define _CHECK_OPS(type, return_val, func) \
	pss_value_ref_ops_t *ops = &_type_ops[type]; \
	if(NULL == ops->func) \
	{ \
		LOG_ERROR("callback function of type "#type" is undefined"); \
		return return_val; \
	}

pss_value_t pss_ref_new(pss_value_ref_type_t type, void* data)
{
	_VALIDATE_TYPE(type, _EVALUE, "Invalid argument: type")
	_CHECK_OPS(type, _EVALUE, mkval);
	void* p_val = ops->mkval(data);
	if(NULL == p_val)
		_ERROR_RETURN(_EVALUE, "mkval error");

	pss_value_t value = {
		.kind = PSS_VALUE_KIND_REF,
		.ref = (pss_value_ref_t*)malloc(sizeof(pss_value_ref_t))
	};
	value.ref->type = type;
	value.ref->p_val = p_val;
	value.ref->ref_count = 1;
	return value;
}

int pss_value_incref(pss_value_t value)
{
	if(PSS_VALUE_KIND_REF == value.kind)
		value.ref->ref_count += 1;
	return 0;
}


int pss_value_decref(pss_value_t value)
{
	if(PSS_VALUE_KIND_REF != value.kind)
		return 0;
	value.ref->ref_count -= 1;
	if(0 == value.ref->ref_count)
	{
		_CHECK_OPS(value.ref->type, -1, free);
		ops->free(value.ref->p_val);
		free(value.ref);
	}
	return 0;
}


pss_value_t pss_value_to_str(pss_value_const_t value)
{
	static const size_t bufsize = 4096;
	char* buf = (char *)malloc(bufsize);
	_CHECK_OPS(value.ref->type, _EVALUE, tostr);
	const char* str = ops->tostr(value.ref->p_val, buf, bufsize);
	if(NULL == str)
	{
		free(buf);
		_ERROR_RETURN(_EVALUE, "Can not convert pss_value to str");
	}
	if(str != buf)
	{
		size_t len = strlen(str) + 1;
		if(len > bufsize)
		{
			len = bufsize - 1;
			buf[len] = '\0';
		}
		memcpy(buf, str, len);
	}
	return pss_ref_new(PSS_VALUE_REF_TYPE_STRING, buf);
}

int pss_value_set_type_ops(pss_value_ref_type_t type, pss_value_ref_ops_t ops)
{
	_VALIDATE_TYPE(type, -1, "Invalid argument: type")
	if(NULL == ops.mkval || NULL == ops.free || NULL == ops.tostr)
	{
		_ERROR_RETURN(-1, "Invalid argument: ops");
	}
	_type_ops[type] = ops;
	return 0;
}


void* pss_value_get_data(pss_value_const_t value)
{
	if(PSS_VALUE_KIND_REF != value.kind)
		return NULL;
	return value.ref->p_val;
}
