/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the page allocator
 * @note this allocator is designed to be thread-safe
 * @file mempool/page.h
 **/
#ifndef __PLUMBER_UTILS_MEMPOOL_PAGE_H__
#define __PLUMBER_UTILS_MEMPOOL_PAGE_H__

/**
 * @brief initialize the page allocator
 * @return the status code
 **/
int mempool_page_init(void);

/**
 * @brief finalize the page allocator
 * @return the status code
 **/
int mempool_page_finalize(void);

/**
 * @brief allocate a signle page
 * @return the allocated page or error code
 **/
void* mempool_page_alloc(void);

/**
 * @brief dealloc a signle page
 * @return the status code
 **/
int mempool_page_dealloc(void* page);

/**
 * @brief set the max number of free page should have in the pool
 * @param npages how many free pages
 * @return status code
 **/
int mempool_page_set_free_page_limit(size_t npages);

/**
 * @brief disable the memory pool
 **/
void mempool_page_disable(int val);

#endif /* __PLUMBER_UTILS_MEMPOOL_PAGE_H__ */
