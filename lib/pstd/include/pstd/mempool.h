/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the memory pool utilies
 * @file pstd/include/pstd/mempool.h
 **/
#ifndef __PSTD_MEMPOOL_H__
#define __PSTD_MEMPOOL_H__

/**
 * @brief allocate memory from the memory pool
 * @param size the size of the memory chunck
 * @return the result pointer, NULL indicates error
 **/
void* pstd_mempool_alloc(uint32_t size);

/**
 * @brief free used memory allocated from the memory pool
 * @param mem the memory to free
 * @return the status code
 **/
int pstd_mempool_free(void* mem);

/**
 * @brief allocate an entire page from the memory pool
 * @return the page allocated or NULL on error case
 **/
void* pstd_mempool_page_alloc();

/**
 * @brief deallocate the entire page which is allocated by pstd_mempool_page_alloc
 * @param page the page to deallocate
 * @return status code
 **/
int pstd_mempool_page_dealloc(void* page);

#endif /* __PSTD_MEMPOOL_H__ */
