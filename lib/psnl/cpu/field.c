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
 * @brief The actual data structure used for the Field lives on the CPU
 **/
typedef struct {
	uintpad_t  __padding__[0];
	size_t     elem_size;      /*!< The element size */
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
 * @brief Reperesent the string representation of a field
 **/
typedef struct {
	const void* data;   /*!< The actual data to be written */
	size_t      size;   /*!< The size of the struct */
} _stream_t;


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

static void* _field_data_new(const void* data)
{
	const _create_param_t* param = (const _create_param_t*)data;

	size_t size = psnl_dim_space_size(param->dim) * param->elem_size + psnl_dim_data_size(param->dim);

	_data_t* ret = (_data_t*)calloc(sizeof(size), 1);

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new CPU field");

	memcpy(ret->dim, param->dim, psnl_dim_data_size(param->dim));
	ret->elem_size = param->elem_size;

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

int psnl_cpu_field_incref(const psnl_cpu_field_t* field)
{
	return psnl_memobj_incref(_get_memory_object_const(field));
}

int psnl_cpu_field_decref(const psnl_cpu_field_t* field)
{
	return psnl_memobj_decref(_get_memory_object_const(field));
}

static int _free(void* mem)
{
	return psnl_memobj_free((psnl_memobj_t*)mem);
}

static void* _open(const void* mem)
{
	_stream_t* ret = pstd_mempool_alloc(sizeof(_stream_t));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the stream object");
	
	const _data_t* data = (const _data_t*)psnl_memobj_get_const((const psnl_memobj_t*)mem, _FIELD_MAGIC);
	
	ret->size = _get_padded_size(psnl_dim_data_size(data->dim)) + psnl_dim_data_size(data->dim) * sizeof(double);

	ret->data = mem;

	return ret;
}

static int _close(void* stream_mem)
{
	return pstd_mempool_free(stream_mem);
}

static int _eos(const void* stream_mem)
{
	const _stream_t* stream = (const _stream_t*)stream_mem;

	return stream->size > 0;
}

static size_t _read(void* __restrict stream_mem, void* __restrict buf, size_t count)
{
	_stream_t* stream = (_stream_t*)stream_mem;

	size_t ret = count;
	if(ret > stream->size) ret = stream->size;

	memcpy(buf, stream->data, ret);
	
	stream->size -= ret;
	stream->data = (const void*)(((const uint8_t*)stream->data) + ret);
	return ret;
}

scope_token_t psnl_cpu_field_commit(psnl_cpu_field_t* field)
{
	if(NULL == field)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	int is_cmt = psnl_memobj_is_committed(_get_memory_object_const(field));

	if(ERROR_CODE(int) == is_cmt)
		ERROR_RETURN_LOG(scope_token_t, "Cannot check if the field has been committed");

	if(is_cmt)
		ERROR_RETURN_LOG(scope_token_t, "Cannot re-committed a token that is already in RLS");

	if(ERROR_CODE(int) == psnl_memobj_set_committed(_get_memory_object(field), 1))
		ERROR_RETURN_LOG(scope_token_t, "Cannot set the commit flag");

	scope_entity_t ent = {
		.data = field,
		.free_func = _free,
		.open_func = _open,
		.close_func = _close,
		.eos_func   = _eos,
		.read_func  = _read
	};

	return pstd_scope_add(&ent);
}

void* psnl_cpu_field_get_data(psnl_cpu_field_t* field, psnl_dim_t const ** dim_buf)
{
	if(NULL == field)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	_data_t* data = (_data_t*)psnl_memobj_get(_get_memory_object(field), _FIELD_MAGIC);

	if(NULL == data)
		ERROR_PTR_RETURN_LOG("Cannot get the field object from the managed reference object");

	if(NULL != dim_buf) 
		*dim_buf = data->dim;

	return data->data + _get_padded_size(psnl_dim_data_size(data->dim));
}

const void* psnl_cpu_field_get_data_const(const psnl_cpu_field_t* field, psnl_dim_t const** dim_buf)
{
	if(NULL == field)
		ERROR_PTR_RETURN_LOG("Invalid arguments");
	
	const _data_t* data = (const _data_t*)psnl_memobj_get_const(_get_memory_object_const(field), _FIELD_MAGIC);

	if(NULL == data)
		ERROR_PTR_RETURN_LOG("Cannot get the field object from the managed reference object");

	
	if(NULL != dim_buf) 
		*dim_buf = data->dim;

	return data->data + _get_padded_size(psnl_dim_data_size(data->dim));
}

const psnl_cpu_field_t* psnl_cpu_field_from_rls(scope_token_t token)
{
	if(ERROR_CODE(scope_token_t) == token)
		ERROR_PTR_RETURN_LOG("Invlaid arguments");

	return (const psnl_cpu_field_t*)pstd_scope_get(token);
}
