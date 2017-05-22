/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdlib.h>
#include <string.h>

#include <utils/log.h>
#include <utils/vector.h>

#include <errno.h>

vector_t* vector_new(size_t elem_size, size_t init_cap)
{
	vector_t* ret;

	ret = (vector_t*)malloc(sizeof(vector_t) + init_cap * elem_size);

	if(NULL == ret)
	{
		LOG_ERROR_ERRNO("cannot allocate memory");
		return NULL;
	}

	ret->_elem_size = elem_size;
	ret->_cap = init_cap;
	ret->_length = 0;

	LOG_DEBUG("created a new vector with elem_size = %zu, initial_cap = %zu", elem_size, init_cap);
	return ret;
}

int vector_free(vector_t* vec)
{
	if(NULL == vec) return 0;

	free(vec);
	return 0;
}


vector_t* vector_append(vector_t* vec, const void* data)
{
	if(NULL == vec) return NULL;

	if(vec->_cap == vec->_length)
	{
		size_t next_cap = vec->_cap * 2;

		size_t next_size = next_cap * vec->_elem_size + sizeof(vector_t);

		vector_t* tmp = (vector_t*)realloc(vec, next_size);
		if(NULL == tmp)
		{
			LOG_ERROR_ERRNO("Could not resize vector %p", vec);
			return NULL;
		}

		vec = tmp;

		LOG_DEBUG("resized vector %p from size %zu to size %zu", vec, vec->_cap, next_cap);

		vec->_cap = next_cap;

	}

	if(NULL != data)
	    memcpy(_vector_get(vec, vec->_length ++), data, vec->_elem_size);
	else
	    vec->_length ++;

	return vec;
}
