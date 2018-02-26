/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>

#include <pstd/scope.h>
#include <pstd/mempool.h>
#include <pstd/types/array.h>

struct _pstd_array_t {
	uint32_t                 committed:1;/*!< Indicates if this object has been committed */
	uint32_t                 capacity;   /*!< The capacity of this array */
	uint32_t                 size;       /*!< The actual size of this array */
	uint32_t                 elem_size;  /*!< The size of element */
	char*                    data;       /*!< The actual data section */
};

static inline int _ensure_size(pstd_array_t* array)
{
	if(array->capacity <= array->size)
	{
		size_t new_size  = array->capacity * array->elem_size * 2;
		char* next_data = (char*)realloc(array->data, new_size);

		if(NULL == next_data)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the array buffer");

		array->data = next_data;
		array->capacity *= 2;
	}

	return 0;
}

pstd_array_t* pstd_array_new(const pstd_blob_model_t* blob_model, size_t init_cap)
{
	if(NULL == blob_model || init_cap == 0)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	pstd_array_t* ret = NULL;
	size_t elem_size = pstd_blob_model_full_size(blob_model);

	if(ERROR_CODE(size_t) == elem_size)
		ERROR_PTR_RETURN_LOG("Cannot get the size of the blob");

	ret = (pstd_array_t*)pstd_mempool_alloc(sizeof(pstd_array_t));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the array");

	memset(ret, 0, sizeof(*ret));
	
	size_t init_size = init_cap * elem_size;

	if(NULL == (ret->data = (char*)malloc(init_size)))
	{
		free(ret);
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the inital array buffer");
	}
	
	ret->capacity = (uint32_t)init_cap;

	return ret;
}

static inline int _array_free(pstd_array_t* array)
{
	if(NULL == array) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(array->data != NULL) free(array->data);

	if(ERROR_CODE(int) == pstd_mempool_free(array))
		ERROR_RETURN_LOG(int, "Cannot dispose the used array");

	return 0;
}

int pstd_array_free(pstd_array_t* array)
{
	if(NULL == array)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(array->committed)
		ERROR_RETURN_LOG(int, "Cannot dispose a commited RLS");

	return _array_free(array);
}

const pstd_array_t* pstd_array_from_rls(scope_token_t token)
{
	if(ERROR_CODE(scope_token_t) == token)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	return (const pstd_array_t*)pstd_scope_get(token);
}

static int _rls_free(void* arr)
{
	pstd_array_t* array = (pstd_array_t*)arr;
	return _array_free(array);
}

scope_token_t pstd_array_commit(pstd_array_t* array)
{
	if(NULL == array)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	if(array->committed)
		ERROR_RETURN_LOG(scope_token_t, "Cannot commit an RLS object that has been committed previously");

	scope_entity_t entity = {
		.data = array,
		.free_func = _rls_free
		/* TODO: support for DRA */
	};

	scope_token_t ret = pstd_scope_add(&entity);

	if(ERROR_CODE(scope_token_t) == ret)
		ERROR_RETURN_LOG(scope_token_t, "Cannot add the entity to scope");

	array->committed = 1;
	return ret;
}

const pstd_blob_t* pstd_array_get(const pstd_array_t* array, uint32_t idx)
{
	if(NULL == array || idx == ERROR_CODE(uint32_t) || idx >= array->size)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	return (const pstd_blob_t*)(array->data + array->elem_size * idx);
}

pstd_blob_t* pstd_array_get_mutable(pstd_array_t* array, uint32_t idx)
{
	if(NULL == array  || (idx != (uint32_t) -1 && idx >= array->size))
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(idx == (uint32_t)-1)
	{
		if(ERROR_CODE(int) == _ensure_size(array))
			ERROR_PTR_RETURN_LOG("Cannot resize the array buffer");
		idx = array->size ++;
	}
	
	return (pstd_blob_t*)(array->data + array->elem_size * idx);
}

int pstd_array_remove(pstd_array_t* array, uint32_t idx)
{
	if(NULL == array || idx == ERROR_CODE(uint32_t) || idx >= array->size)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(array->size - idx > 1)
		memmove(array->data + array->elem_size * idx, array->data + array->elem_size * (idx + 1), array->elem_size * (array->size - idx - 1));

	array->size --;

	return 0;
}

uint32_t pstd_array_size(const pstd_array_t* array)
{
	if(NULL == array)
		ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	return array->size;
}
