/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>
#include <pss/closure.h>
/**
 * @brief The actual data strcuture of the closure
 **/
struct _pss_closure_t {
	pss_frame_t* env;    /*!< The environment frame */
	const pss_bytecode_segment_t* code;  /*!< The code to execute */
	const pss_bytecode_module_t*  module;/*!< The module contains the code */
};
/**
 * @brief Make a value from the give parameter
 * @param param The closure creation param
 * @return The newly created closure object
 **/
static void* _mkval(void* param)
{
	if(NULL == param) ERROR_PTR_RETURN_LOG("Invalid arguments");
	pss_closure_creation_param_t* cp = (pss_closure_creation_param_t*)param;

	pss_closure_t* ret = (pss_closure_t*)calloc(1, sizeof(ret[0]));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the closure object");

	if(NULL == (ret->env = pss_frame_new(cp->env)))
		ERROR_LOG_GOTO(ERR, "Cannot copy the given environment frame");

	if(NULL == (ret->code = pss_bytecode_module_get_seg(cp->module, cp->segid)))
		ERROR_LOG_GOTO(ERR, "Cannot get the target segment from the module");

	ret->module = cp->module;

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->env) pss_frame_free(ret->env);
		free(ret);
	}

	return NULL;
}

/**
 * @brief Dispose the used closure
 * @param closure_mem The memory occupied by the closure
 * @return status code
 **/
static int _free(void* closure_mem)
{
	int rc = 0;
	if(NULL == closure_mem)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_closure_t* closure = (pss_closure_t*)closure_mem;

	if(NULL != closure->env && ERROR_CODE(int) == pss_frame_free(closure->env))
		rc = ERROR_CODE(int);

	free(closure);
	return rc;
}

/**
 * @brief Convert a closure to string
 * @param ptr The pointer to the clsoure
 * @param buf The string buffer
 * @param bufsize The size of the string buffer
 * @return The converted string
 **/
static const char* _tostr(const void* ptr, char* buf, size_t bufsize)
{
	snprintf(buf, bufsize, "<closure@%p>", ptr);
	return buf;
}

int pss_closure_init()
{
	pss_value_ref_ops_t clsoure_ops = {
		.mkval = _mkval,
		.free  = _free,
		.tostr = _tostr
	};

	return pss_value_ref_set_type_ops(PSS_VALUE_REF_TYPE_CLOSURE, clsoure_ops);
}

int pss_closure_finalize()
{
	return 0;
}

pss_frame_t* pss_closure_get_frame(const pss_closure_t* closure)
{
	if(NULL == closure) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return pss_frame_new(closure->env);
}

const pss_bytecode_segment_t* pss_closure_get_code(const pss_closure_t* closure)
{
	if(NULL == closure) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return closure->code;
}

const pss_bytecode_module_t* pss_closure_get_module(const pss_closure_t* closure)
{
	if(NULL == closure) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return closure->module;
}
