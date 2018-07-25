/**
 * Copyright (C) 2017, Hao Hou
 * Copyright (C) 2017, Feng Liu
 **/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>

#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>

/**
 * @brief The operation functions for all the type accepted by the VM
 **/
static pss_value_ref_ops_t _type_ops[PSS_VALUE_REF_TYPE_COUNT];

/**
 * @brief The error value
 **/
static const pss_value_t _EVALUE = { .kind = PSS_VALUE_KIND_ERROR };

/**
 * @brief The actual data structure for a PSS value reference
 **/
struct _pss_value_ref_t {
	uint32_t             refcnt;/*!< The reference counter */
	pss_value_ref_type_t type;  /*!< The type code */
	void*                val;   /*!< The pointer to the object this reference refers */
};

/**
 * @brief Verify the callback function of this type is defined
 **/
#define _CHECK_TYPE(type, label) \
    do {\
	    if(type < 0 || type >= PSS_VALUE_REF_TYPE_COUNT)\
	        ERROR_LOG_GOTO(label, "Invalid type code");\
    } while(0)

#define _CHECK_OPS(type, func, label) \
    _CHECK_TYPE(type, label); \
    pss_value_ref_ops_t *ops = &_type_ops[type]; \
    if(NULL == ops->func) ERROR_LOG_GOTO(label, "Undefined Operations");

pss_value_t pss_value_ref_new(pss_value_ref_type_t type, void* data)
{
	void* val = NULL;

	pss_value_t value = {
		.kind = PSS_VALUE_KIND_REF
	};

	_CHECK_OPS(type, mkval, ERR);

	if(NULL == (value.ref = (pss_value_ref_t*)malloc(sizeof(value.ref[0]))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the value reference");

	if(NULL == (val = ops->mkval(data)))
		ERROR_LOG_GOTO(ERR, "Cannot make value from the input pointer");

	value.ref->type = type;
	value.ref->val = val;
	value.ref->refcnt = 0;
	return value;

ERR:
	if(NULL != val) ops->free(val);
	if(NULL != value.ref) free(value.ref);
	return _EVALUE;
}

pss_value_ref_type_t pss_value_ref_type(pss_value_t value)
{
	if(value.kind != PSS_VALUE_KIND_REF)
		ERROR_RETURN_LOG(pss_value_ref_type_t, "Invalid arguments");

	return value.ref->type;
}

int pss_value_incref(pss_value_t value)
{
	if(PSS_VALUE_KIND_REF == value.kind)
		value.ref->refcnt ++;
	return 0;
}


int pss_value_decref(pss_value_t value)
{
	if(PSS_VALUE_KIND_REF != value.kind)
		return 0;

	if(value.ref->refcnt > 0) value.ref->refcnt --;

	if(0 == value.ref->refcnt)
	{
		_CHECK_OPS(value.ref->type, free, ERR);
		ops->free(value.ref->val);
		free(value.ref);
	}
	return 0;
ERR:
	return ERROR_CODE(int);
}

static inline const char* _value_to_str(pss_value_t value, char* buf, size_t sz)
{
	static char default_buf[4096];
	if(buf == NULL)
	{
		buf = default_buf;
		sz = sizeof(default_buf);
	}

	const char* ret = NULL;
	if(value.kind == PSS_VALUE_KIND_REF)
	{
		_CHECK_OPS(value.ref->type, tostr, ERR);
		if(NULL == (ret = ops->tostr(value.ref->val, buf, sz)))
			ERROR_LOG_GOTO(ERR, "Cannot dump the object to string");
	}
	else if(value.kind == PSS_VALUE_KIND_NUM)
	{
		snprintf(buf, sz, "%"PRId64, value.num);
		ret = buf;
	}
	else if(value.kind == PSS_VALUE_KIND_BUILTIN)
	{
		snprintf(buf, sz, "<built-in@%p>", value.builtin);
		ret = buf;
	}
	else if(value.kind == PSS_VALUE_KIND_UNDEF)
		ret = "undefined";

	return ret;
ERR:
	return NULL;
}

size_t pss_value_strify_to_buf(pss_value_t value, char* buf, size_t sz)
{
	if(value.kind == PSS_VALUE_KIND_ERROR || NULL == buf || sz < 1)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const char* str = _value_to_str(value, buf, sz);
	if(NULL == str) ERROR_RETURN_LOG(size_t, "Cannot stringify the value");

	size_t ret = strlen(str);

	if(buf == str) return ret;

	if(sz < ret) ret = sz - 1;

	buf[ret] = 0;
	memcpy(buf, str, ret);

	return ret;
}

pss_value_t pss_value_to_str(pss_value_t value)
{
	char* buf = NULL;
	if(PSS_VALUE_KIND_ERROR == value.kind)
		ERROR_LOG_GOTO(ERR, "Invalid arguments");

	const char* str = _value_to_str(value, NULL, 0);

	if(NULL == str) ERROR_LOG_GOTO(ERR, "Cannot stringify the value");

	size_t len = strlen(str) + 1;
	buf = (char*)malloc(len);
	memcpy(buf, str, len);

	return pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, buf);
ERR:
	if(NULL == buf) free(buf);
	return _EVALUE;
}

int pss_value_ref_set_type_ops(pss_value_ref_type_t type, pss_value_ref_ops_t ops)
{
	_CHECK_TYPE(type, ERR);
	if(NULL == ops.mkval || NULL == ops.free || NULL == ops.tostr)
		ERROR_LOG_GOTO(ERR, "Object operations is not fully defined");
	_type_ops[type] = ops;
	return 0;
ERR:
	return ERROR_CODE(int);
}


void* pss_value_get_data(pss_value_t value)
{
	if(PSS_VALUE_KIND_REF != value.kind) return NULL;
	return value.ref->val;
}

