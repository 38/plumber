/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The memory pool used to allocate the object which has a fixed size
 * @file mempool/objpool.h
 * @note all the memory pool must be thread-safe
**/

#ifndef __PLUMBER_UTILS_MEMPOOL_OBJECT_H__
#define __PLUMBER_UTILS_MEMPOOL_OBJECT_H__

/**
 * @brief The thread local pool policy
 * @note This is the description of the behavior of the thread local object memory pool,
 *       the reason for why we need this is. In some case, the memory object allocated by
 *       one thread may be disposed by another thread. What make the things worse is there
 *       are some thread which is allocating memory all the time, at the same time there's
 *       another groups of thread which is only deallocating the objects. As the result,
 *       the memory cached in the second group of threads can not be reused unless it reach the
 *       limit of the TLP size.  <br/>
 *       One good instance of this is in the event loop it creates the pipe handle when there is
 *       IO event. However, it rarely dispose a pipe handle. And all the handle it created will be
 *       sent to the worker thread where the pipe handle gets disposed. This makes the pipe handle
 *       cached in the worker thread, however, the IO thread don't need too many IO handle. This makes
 *       the memory usage increases. <br/>
 *       To address this, we need to introduce some additional mechanism, which allows we set a limit
 *       of the cached object for different type of thread. <br/>
 *       On the other hand, because the event loop is always starving, it always ask the global pool for
 *       the new pipe handle. So we also need another global allocation unit param, which says instead just
 *       allocate one object once you hold the global memory pool mutex, allocating global_alloc_unit objects
 *       instead.
 **/
typedef struct {
	uint32_t   cache_limit;     /*!< the size of the thread local pool (this actually guareentee the number of objects is not larger than 2 * cache_limit) */
	uint32_t   alloc_unit;      /*!< the global allocation unit, how many object we want to allocate from the local pool */
} mempool_objpool_tlp_policy_t;
/**
 * @brief the incomplete type for a fix-sized mem pool
 **/
typedef struct _mempool_objpool_t mempool_objpool_t;

/**
 * @brief create a new fix-sized pool
 * @param size the  size of each object in the pool
 * @return the newly created pool, or NULL on error cases
 **/
mempool_objpool_t* mempool_objpool_new(uint32_t size);

/**
 * @brief deallocate a used memory pool
 * @param pool the pool to dispose
 * @return the status code
 **/
int mempool_objpool_free(mempool_objpool_t* pool);

/**
 * @brief allocate a new object from the pool
 * @param pool the target memory pool
 * @return the allocated memory, NULL on error cases
 **/
void* mempool_objpool_alloc(mempool_objpool_t* pool);

/**
 * @brief return the pool memory to the pool
 * @param pool the target pool
 * @param mem  the memory to dispose
 * @return the stauts code
 **/
int mempool_objpool_dealloc(mempool_objpool_t* pool, void* mem);

/**
 * @brief set the flag indicates if the objpool needs to be disabled
 * @param val the value of the disable falg
 * @return status code
 **/
int mempool_objpool_disabled(int val);

/**
 * @brief get the size of the object (include the padding)
 * @param pool the target memory pool
 * @return the size of the object or error code
 **/
uint32_t mempool_objpool_get_obj_size(const mempool_objpool_t* pool);

/**
 * @brief get the page managed by this object mem pool
 * @param pool the target pool
 * @return the number of pages or error code
 **/
uint32_t mempool_objpool_get_page_count(const mempool_objpool_t* pool);

/**
 * @brief set the global allocation unit for the given types of threads
 * @note  the global allocation unit means how many object we want to get from the global object pool to the thread local pool
 *        once we locked the mutex. <br/>
 *        Combine with the function mempool_objpool_set_thread_cache_limit, this is used to resolve the issue that the memory flows
 *        from one thread to another, which cause high memory usage. <br/>
 *        By having this two function, if the thread like the event loop just allocates the pipe handle but alomost nevere dispose it;
 *        at the same time, the worker thread just need to use limited number of pipe handle. However, the event loop allocated pipe handle
 *        will be most likely disposed by the worker thread. Which makes the worker threads gets large number of pipe handle, but cannot
 *        use them at all. So the memory usage is high, beucase the worker thread do not actually make a good use of the memory until the
 *        thread local pool reached the size limit. <br/>
 *        By having these two function, we can make the event_look have a larger global allocation unit. At the same time, we can make the
 *        worker thread's cache limit very small, which means we want to return the memory very frequently. (For example, for each 128 dispose)
 *        At the same time the event loop locks the mutex every cache_limit times. <br/>
 *        So by having this  configuration, the problem can be solved. <br/>
 *        We should call this function right after the pool is created, otherwise, the behavior is undefined
 * @param pool the object pool to set
 * @param thread_mask the types of thread we want to use
 * @param policy the thread local policy we want to set
 * @return status code
 **/
int mempool_objpool_set_thread_policy(mempool_objpool_t* pool, unsigned thread_mask, mempool_objpool_tlp_policy_t policy);
#endif /* __PLUMBER_UTILS_MEMPOOL_BLOCK_H__ */
