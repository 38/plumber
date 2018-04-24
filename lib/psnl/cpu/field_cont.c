/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <string.h>
#include <pstd.h>

#include <psnl/dim.h>
#include <psnl/memobj.h>
#include <psnl/cpu/field_cont.h>

#define _OBJ_MAGIC 0x314aaf027abdef12ull

/**
 * @brief The internal continuation data
 **/
typedef struct {
	const void* __restrict           lhs;       /*!< The left-hand-side */
	psnl_cpu_field_cont_eval_func_t  eval_func; /*!< The evaluation function */
	psnl_cpu_field_cont_free_func_t  free_func; /*!< The free function */
	uint64_t                         tag;       /*!< The tag: meaning is undefined, might be useful for JIT */
	uintpad_t __padding__[0];
	psnl_dim_t                       range[0];     /*!< The valid range of the result */
} _cont_t;

/**
 * @brief The object that is used to convert object from external type to the internal type
 **/
typedef union {
	psnl_cpu_field_cont_t* external;    /*!< The external data object */
	psnl_memobj_t*         internal;    /*!< The internal data object */
} _view_t;

static void* _create_data(const void* user_data)
{
	const psnl_cpu_field_cont_desc_t* desc = (const psnl_cpu_field_cont_desc_t*)user_data;

	size_t size = sizeof(_cont_t) + psnl_dim_data_size(desc->range);

	if(size > 4096)
		ERROR_PTR_RETURN_LOG("Invalid arguments: number of dimensions is too large");

	_cont_t* data_obj = (_cont_t*)pstd_mempool_alloc((uint32_t)size);

	if(NULL == data_obj)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the data object");

	memcpy(data_obj->range, desc->range, psnl_dim_data_size(desc->range));

	data_obj->lhs = desc->lhs;
	data_obj->eval_func = desc->eval;
	data_obj->free_func = desc->free;
	data_obj->tag = desc->tag;

	return data_obj;
}

static int _dispose_data(void* obj, void* user_data)
{
	(void)user_data;

	_cont_t* cont = (_cont_t*)obj;

	int ret = cont->free_func(cont->lhs);

	if(ERROR_CODE(int) == ret)
		LOG_ERROR("Cannot dispose the LHS");

	if(ERROR_CODE(int) == pstd_mempool_free(obj))
		ERROR_RETURN_LOG(int, "Cannot dispose the used continuation");

	return ret;
}

psnl_cpu_field_cont_t* psnl_cpu_field_cont_new(const psnl_cpu_field_cont_desc_t* cont_desc)
{
	if(NULL == cont_desc)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	psnl_memobj_param_t ret_obj_param = {
		.magic      = _OBJ_MAGIC,
		.obj        = NULL,
		.create_cb  = _create_data,
		.dispose_cb = _dispose_data,
		.dispose_cb_data = NULL
	};

	_view_t ret_obj = {
		.internal = psnl_memobj_new(ret_obj_param, cont_desc)
	};

	if(NULL == ret_obj.internal)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory object for the continuation object");

	return ret_obj.external;
}
