/**
 * Copyright (C) 2017, Kejun Li
 **/
/**
 * @brief the memory pool that only allow allocate
 * @note the memory allocated will be deallocate'd when the entrie pool gets free'd
 * @file mempool/oneway.h
 **/

#ifndef __PLUMBER_UTILS_MEMPOOL_ONEWAY_H__
#define __PLUMBER_UTILS_MEMPOOL_ONEWAY_H__

/**
 * @brief the incomplete type for a one-way memory pool'
 **/
typedef struct _mempool_oneway_t mempool_oneway_t;

/**
 * @brief create a new oneway memory pool
 * @return the newly created memory pool, NULL on error
 **/
mempool_oneway_t* mempool_oneway_new(size_t init_size);

/**
 * @brief allocate memory with given size
 * @param size the required size
 * @param pool the memory pool
 * @return the pointer to the memory allocated
 **/
void* mempool_oneway_alloc(mempool_oneway_t* pool, size_t size);

/**
 * @brief dispose a newd memory pool, and release all the memory it occupies
 * @param pool the target pool
 **/
int mempool_oneway_free(mempool_oneway_t* pool);

#endif

