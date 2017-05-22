/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief bitmask, a fast unique ID allocator
 * @details bitmask is a data structure used to allocate / deallocate the integer ID dynamically
 *         it's based on a tree structure bit map
 * @note for perfomance reason, this file doesn't do any argument check
 **/
#include <stdint.h>
#include <stdlib.h>

#ifndef __PLUMBER_UTILS_BITMASK_H__
#define __PLUMBER_UTILS_BITMASK_H__

/**
 * @brief the bitmask data structure
 **/
typedef struct _bitmask_t bitmask_t;

/**
 * @brief create a new bit mask
 * @param size the size of the bitmask
 * @return the newly created bitmask, NULL incidates an error
 **/
bitmask_t* bitmask_new(size_t size);

/**
 * @brief dispose a bitmask
 * @param bitmask the target bitmask
 * @return status code
 **/
int bitmask_free(bitmask_t* bitmask);

/**
 * @brief allocate an unused integer from the bitmask
 * @param bitmask the target bit mask
 * @return error code or the allocated id
 **/
size_t bitmask_alloc(bitmask_t* bitmask);

/**
 * @brief deallocate an used integer and make it able to reuse later
 * @param bitmask the target bitmask
 * @param id the id to deallocate
 * @return status code
 **/
int bitmask_dealloc(bitmask_t* bitmask, size_t id);

/**
 * @brief check if bitmask is full
 * @param bitmask the target bitmask
 * @return error code, or 0 if the bitmask is not full, 1 if it's full
 **/
int bitmask_full(const bitmask_t* bitmask);

/**
 * @brief clear the bit mask, set all bits to 0
 * @param bitmask the target bitmask
 * @return status code
 **/
int bitmask_clear(bitmask_t* bitmask);

/**
 * @brief check if this bit mask is entirely emptry
 * @param bitmask the target bitmask
 * @return status code
 **/
int bitmask_empty(const bitmask_t* bitmask);

#endif /* __PLUMBER_UTILS_BITMASK_H__ */
