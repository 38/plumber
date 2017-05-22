/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file vector.h
 * @brief vector, the dynamic sized array
 * @note  this file do not requires initialization and finalization
 **/
#ifndef __VECTOR_H__
#define __VECTOR_H__
#include <stdint.h>
#include <utils/static_assertion.h>

/** @brief the vector type */
typedef struct {
	size_t _elem_size; /*!< the element size */
	size_t _cap;       /*!< the capacity in number of elements */
	size_t _length;	  /*!< the number of elements in the vector */
	uintptr_t __padding__[0];
	char _data[0];	  /*!< the actual data */
} vector_t;

STATIC_ASSERTION_LAST(vector_t, _data);
STATIC_ASSERTION_SIZE(vector_t, _data, 0);

/**
 * @brief create a new vector
 * @param elem_size the size of the element
 * @param init_cap  the initial capacity
 **/
vector_t* vector_new(size_t elem_size, size_t init_cap);

/**
 * @brief free the memory that occupied by the vector
 * @note don't forget also call free function on each elements, the vector does not responsible for freeing the element
 * @param vec the vector to free
 * @return < 0 on error
 **/
int vector_free(vector_t* vec);

/**
 * @brief access the length of the vector
 * @note this function do not check the parameters, *it may cause crash*
 * @return the number of elements
 **/
static inline size_t vector_length(const vector_t* vec)
{
	return vec->_length;
}

/**
 * @brief get the N-th element in a read-only pointer
 * @note this function do not check parameters
 * @param vec the vector
 * @param n the n-th element
 * @return the pointer for N-th element
 **/
static inline const void * _vector_get_const(const vector_t* vec, size_t n)
{
	const char* data = vec->_data;
	size_t offset = vec->_elem_size * n;
	return data + offset;
}

/**
 * @brief get the N-th element in a read-write pointer
 * @param vec the vector
 * @param n the n-th element
 * @return the pointer to N-th element
 **/
static inline void * _vector_get(vector_t* vec, size_t n)
{
	char* data = vec->_data;
	size_t offset = vec->_elem_size * n;
	return data + offset;
}

#define VECTOR_GET_CONST(type, vec, n) ((type const*)_vector_get_const(vec, n))
#define VECTOR_GET(type, vec, n) ((type*)_vector_get(vec, n))

/**
 * @brief remove all the element in the vector
 * @param vec the vector to clear
 * @return nothing
 **/
static inline void vector_clear(vector_t* vec)
{
	vec->_length = 0;
}

/**
 * @brief append the element to the end of the vector
 * @note this will copy the data
 * @param vec the vector
 * @param data the pointer to the actual data, if NULL is passed, just expand the vector but do not copy any data
 * @return the vector after this operation, NULL on error
 **/
vector_t* vector_append(vector_t* vec, const void* data);

#endif /* __VECTOR_H__ */


