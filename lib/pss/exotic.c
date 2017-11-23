/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <package_config.h>
#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/exotic.h>

/**
 * @brief The actual data structure for an exotic object
 **/
struct _pss_exotic_t {
	uint32_t  magic_num;  /*!< The magic number of the object */
	const char* typename; /*!< The typename of the object */
	/**
	 * @brief Dispose a use exotic object
	 * @param mem The memomry to dispose
	 * @return status code
	 **/
	int (*dispose)(void* mem);

	void* data; /*!< The actual data */
};

static void* _mkval(void* param)
{
	if(NULL == param) ERROR_PTR_RETURN_LOG("Invalid arguments");

	pss_exotic_t* ret = (pss_exotic_t*)malloc(sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new exotic object");

	pss_exotic_creation_param_t* cp = (pss_exotic_creation_param_t*)param;
	ret->magic_num = cp->magic_num;
	ret->dispose = cp->dispose;
	ret->data = cp->data;
	ret->typename = cp->type_name == NULL ? "Unknown" : cp->type_name;

	return ret;
}

static const char* _tostr(const void* ptr, char* buf, size_t bufsize)
{
	const pss_exotic_t* obj = (const pss_exotic_t*)ptr;
	snprintf(buf, bufsize, "<exotic_obj:%s@%p>", obj->typename, ptr);
	return buf;
}

static int _free(void* mem)
{
	pss_exotic_t* obj = (pss_exotic_t*)mem;
	if(NULL == obj) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;

	if(NULL != obj->dispose && ERROR_CODE(int) == obj->dispose(obj->data))
	    rc = ERROR_CODE(int);

	free(obj);

	return rc;
}



int pss_exotic_init()
{
	pss_value_ref_ops_t exotic_ops = {
		.mkval = _mkval,
		.free  = _free,
		.tostr = _tostr
	};

	return pss_value_ref_set_type_ops(PSS_VALUE_REF_TYPE_EXOTIC, exotic_ops);
}

int pss_exotic_finalize()
{
	return 0;
}

void* pss_exotic_get_data(pss_exotic_t* obj, uint32_t magic_num)
{
	if(NULL == obj) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(obj->magic_num != magic_num) ERROR_PTR_RETURN_LOG("Magic number mismatch");

	return obj->data;
}
