/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <psnl/dim.h>
#include <psnl/memobj.h>
#include <psnl/cpu/field.h>

#define _FIELD_MAGIC 0xcf276354ff00aabbull

/**
 * @brief The actual data structure used for the Double Field lives on the CPU
 **/
typedef struct {
	uintpad_t  __padding__[0];
	psnl_dim_t dim[0];         /*!< The dimensional data */
	char       data[0];        /*!< The actual data section */
} _data_t;

/**
 * @brief The data structure that is used for allocating memory for a field
 **/
typedef struct {
	size_t             elem_size;   /*!< The size of the element in the field */
	const psnl_dim_t*  dim;         /*!< The dimension specification */
} _create_param_t;


/**
 * @brief Convert the memory object from a field dummy type
 **/
static inline psnl_memobj_t* _get_memory_object(psnl_cpu_field_t* field)
{
	return (psnl_memobj_t*)field;
}

/**
 * @brief Convert the const memory object from a field dummy type
 **/
static inline const psnl_memobj_t* _get_memory_object_const(const psnl_cpu_field_t* field)
{
	return (const psnl_memobj_t*)field;
}

/**
 * @brief Get the padded size from the original one
 * @param size The size of the padded size
 * @return the padded size
 **/
static inline size_t _get_padded_size(size_t size) 
{
	size_t rem = (size & (sizeof(uintpad_t) - 1));
	if(rem > 0)
		return size - rem + sizeof(uintpad_t);
	else
		return size;
}

static void* _field_data_new(void* data)
{
	const _create_param_t* param = (const _create_param_t*)data;

	size_t size = psnl_dim_space_size(param->dim) * param->elem_size + psnl_dim_data_size(param->dim);

	_data_t* ret = (_data_t*)calloc(sizeof(size), 1);

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new CPU field");

	memcpy(ret->dim, param->dim, psnl_dim_data_size(param->dim));

	return ret;
}

static int _field_data_free(void* obj, void* data)
{
	(void)data;
	free(obj);
	return 0;
}

psnl_cpu_field_t* psnl_cpu_field_new(const psnl_dim_t* dim, size_t elem_size)
{
	if(NULL == dim || elem_size == 0)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	psnl_memobj_param_t obj_desc = {
		.magic      = _FIELD_MAGIC,
		.create_cb  = _field_data_new,
		.dispose_cb = _field_data_free,
		.dispose_cb_data = NULL
	};

	_create_param_t create_param = {
		.dim = dim,
		.elem_size = elem_size
	};

	return (psnl_cpu_field_t*)psnl_memobj_new(obj_desc, &create_param);
}
