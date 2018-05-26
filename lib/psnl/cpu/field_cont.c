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
	psnl_cpu_field_cont_free_func_t  free_func; /*!< The free function: Since the continuation itself also holds the reference to the RHS, so we need a function for this */
	uint64_t                         tag;       /*!< Tag: meaning is undefined, might be useful for JIT */
	uintpad_t __padding__[0];
	psnl_dim_t                       range[0];  /*!< The valid range of the result */
} _cont_t;

/**
 * @brief The object that is used to convert object from external type to the internal type
 **/
typedef union {
	psnl_cpu_field_cont_t* external;    /*!< The external data object */
	psnl_memobj_t*         internal;    /*!< The internal data object */
} _view_t;

/**
 * @brief Similar to the _view_t type but it's the constant version
 **/
typedef union {
	const psnl_cpu_field_cont_t* external;    /*!< The external data object */
	const psnl_memobj_t*         internal;    /*!< The internal data object */
} _const_view_t;


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

int psnl_cpu_field_cont_free(psnl_cpu_field_cont_t* cont)
{
	if(NULL == cont)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_view_t view = {
		.external = cont
	};

	int committed = psnl_memobj_is_committed(view.internal);

	if(ERROR_CODE(int) == committed || committed > 0)
		ERROR_RETURN_LOG(int, "Refusing to dispose a committed RLS object from application code");

	return psnl_memobj_free(view.internal);
}

int psnl_cpu_field_cont_incref(const psnl_cpu_field_cont_t* cont)
{
	if(NULL == cont)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_const_view_t view = {
		.external = cont
	};

	return psnl_memobj_incref(view.internal);
}

int psnl_cpu_field_cont_decref(const psnl_cpu_field_cont_t* cont)
{
	if(NULL == cont)
		ERROR_RETURN_LOG(int,  "Invalid arguements");

	_const_view_t view = {
		.external = cont
	};

	return psnl_memobj_decref(view.internal);
}

static int _cont_free(void* obj)
{
	return psnl_memobj_decref((psnl_memobj_t*)obj);
}

scope_token_t psnl_cpu_field_cont_commit(psnl_cpu_field_cont_t* cont)
{
	if(NULL == cont)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");
	
	_view_t view = {
		.external = cont
	};

	int committed = psnl_memobj_is_committed(view.internal);

	if(committed == ERROR_CODE(int) || committed > 0)
		ERROR_RETURN_LOG(scope_token_t, "Cannot commit the same object twice");

	scope_entity_t ent = {
		.data = cont,
		.free_func = _cont_free,
	};

	return pstd_scope_add(&ent);
}

int psnl_cpu_field_cont_value_at(const psnl_cpu_field_cont_t* cont, uint32_t ndim, int32_t* __restrict pos, void* __restrict buf)
{
	if(NULL == cont || NULL == pos || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_const_view_t view = {
		.external = cont
	};

	const _cont_t* cont_obj = (const _cont_t*)psnl_memobj_get_const(view.internal, _OBJ_MAGIC);

	if(NULL == cont_obj)
		ERROR_RETURN_LOG(int, "Cannot get the actual continuation object");

#ifndef FULL_OPTIMIZATION 
	/* We won't validate the dimension when we compile fully optmized */
	if(cont_obj->range->n_dim != ndim)
		ERROR_RETURN_LOG(int, "Invalid dimension");

	uint32_t i;
	for(i = 0; i < ndim; i ++)
		if(cont_obj->range->dims[i][0] < pos[i] ||
		   cont_obj->range->dims[i][1] >= pos[i])
			ERROR_RETURN_LOG(int, "Invalid position");
#else
	(void)ndim;
#endif

	cont_obj->eval_func(pos, cont_obj->lhs, buf);

	return 0;
}
