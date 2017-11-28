/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include <predict.h>
#include <constants.h>
#include <error.h>
#include <utils/log.h>
#include <utils/static_assertion.h>

#include <utils/mempool/objpool.h>
#include <utils/mempool/page.h>

#include <utils/thread.h>

extern void  __libc_free(void* ptr);

/**
 * @brief represents a signle page
 **/
typedef struct _page_t {
	struct _page_t* next;   /*!< the next page in the page list */
	uint32_t        unused_start; /*!< the page offset of the unused memory inside this page */
	void*           page;   /*!< the actual page */
} _page_t;

/**
 * @brief represent a allocated object from the pool
 **/
typedef struct _cached_object_t {
	struct _cached_object_t* next;   /*!< the next object in the list */
	struct _cached_object_t* prev;   /*!< the previous object in the list */
} _cached_object_t;

/**
 * @brief the actual memory pool data structure
 **/
struct _mempool_objpool_t {
	uint32_t                     page_count;                     /*!< the number of pages */
	uint32_t                     obj_size;                       /*!< the size of each object in the pool */
	_page_t*                     pages;                          /*!< the pages used by the pool */
	_cached_object_t*            cached;                         /*!< the cahced object list */
	pthread_mutex_t              mutex;                          /*!< the mutex use for the multi-thread pool */
	thread_pset_t*               local_pool;                     /*!< the thread local object pool */
	mempool_objpool_tlp_policy_t policy[THREAD_NUM_TYPES];       /*!< the allocation policy for each type of thread */
};

/**
 * @brief the thread local pool
 **/
typedef struct {
	uint32_t            count;   /*!< the number of object cahced */
	_cached_object_t*   begin;   /*!< the cached objects list begin */
	_cached_object_t*   exceeded;/*!< the point that the cached object is more than the maximum value allowed */
	_cached_object_t*   end;     /*!< the end of the cached objects */
} _thread_local_pool_t;

/**
 * @brief the global flags indicates if we want to turn off the pool.
 * @note this is useful because the memory pool will hide the memory leak in testing envrionment.
 *       By disabling the memory pool the testing envrionment will be able to catch the memory leak
 **/
static int _pool_disabled = 0;

/**
 * @brief we have 65536 thread local cache at most
 **/
static uint32_t _thread_object_max = 0x10000;

#ifndef FULL_OPTIMIZATION
/**
 * @brief the size of one page in current operating system
 **/
static uint32_t _pagesize = 0;
#else
static const uint32_t _pagesize = 4096;
#endif

/**
 * @brief allocate a new page for the memory pool
 * @return the object for that page
 **/
static inline _page_t* _page_new(void)
{
	_page_t* ret = (_page_t*)malloc(sizeof(_page_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new page");

	if(NULL == (ret->page = mempool_page_alloc()))
	{
		free(ret);
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate the page for the page object");
	}

	ret->next = NULL;
	ret->unused_start = 0;

	LOG_DEBUG("A new page has been allocated");
	return ret;
}

/**
 * @brief check if the pool is disabled
 * @param pool the memory pool
 * @return if the pool is disabled
 **/
static inline int _is_pool_disabled(mempool_objpool_t* pool)
{
	if(_pool_disabled) return 1;

	if(pool->obj_size > _pagesize)
	{
		LOG_WARNING("The memory object size is larger than one page, fix-sized pool disabled");
		return 1;
	}

	return 0;
}

/**
 * @brief convert current thread type to the index in the policy array, -1 indicates this is
 *        an generic thread
 * @return the index or THREAD_NUM_TYPES when this is a generic type
 **/
static inline uint32_t _thread_type_to_idx(void)
{
	thread_type_t thread_type = thread_get_current_type();

	switch(thread_type)
	{
		case THREAD_TYPE_EVENT:  return 0;
		case THREAD_TYPE_WORKER: return 1;
		case THREAD_TYPE_IO:     return 2;
		case THREAD_TYPE_ASYNC:  return 3;
		default:                 return 4;
	}
}
STATIC_ASSERTION_EQ_ID(THREAD_NUM_IS_4, THREAD_NUM_TYPES, 4);

/**
 * @brief get the current cached object limit
 * @param pool the pool we want to get
 * @return the cached object limit for current thread, or error code
 **/
static inline uint32_t _get_current_thread_cache_limit(const mempool_objpool_t* pool)
{
	uint32_t idx = _thread_type_to_idx();

	if(PREDICT_FALSE(idx >= THREAD_NUM_TYPES))
	    return _thread_object_max;

	return pool->policy[idx].cache_limit;
}

/**
 * @brief the the global allocation unit for current thread and current pool
 * @param pool the pool we want to get
 * @return status code
 **/
static inline uint32_t _get_current_global_alloc_unit(const mempool_objpool_t* pool)
{
	uint32_t idx = _thread_type_to_idx();
	if(PREDICT_FALSE(idx >= THREAD_NUM_TYPES))
	    return UTILS_THREAD_GENERIC_ALLOC_UNIT;

	return pool->policy[idx].alloc_unit;
}

/**
 * @brief the allocation function for a local thread pool
 * @param tid the thread id
 * @param data the additional data
 * @return the newly created pool
 **/
static inline void* _thread_pool_alloc(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;
#ifdef LOG_DEBUG_ENABLED
	const mempool_objpool_t* parent = (const mempool_objpool_t*)data;
	LOG_DEBUG("Allocating local thread pool for thread %u, parent = %p", tid, parent);
#endif

	_thread_local_pool_t* ret = (_thread_local_pool_t*)malloc(sizeof(_thread_local_pool_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the thread local pool");
	ret->begin = ret->exceeded = ret->end = 0;
	ret->count = 0;
	return ret;
}

/**
 * @brief dispose a used thread local pool
 * @param mem the memory to dispose
 * @param data the additional data
 * @return status code
 **/
static inline int _thread_pool_free(void* mem, const void* data)
{
	(void)data;
	free(mem);
	return 0;
}

mempool_objpool_t* mempool_objpool_new(uint32_t size)
{
	mempool_objpool_t* ret = (mempool_objpool_t*)malloc(sizeof(mempool_objpool_t));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the memory pool");

#ifndef FULL_OPTIMIZATION
	if(_pagesize == 0) _pagesize = (uint32_t)getpagesize();
#endif

	if(size < sizeof(_cached_object_t)) size = sizeof(_cached_object_t);
	if(size % sizeof(uintptr_t) > 0) size = (uint32_t)((size / sizeof(uintptr_t) + 1) * sizeof(uintptr_t));

	LOG_DEBUG("Object memory pool allocation object size: %u", size);

	ret->obj_size = size;
	ret->pages = NULL;
	ret->cached = NULL;
	ret->page_count = 0;

	if(NULL == (ret->local_pool = thread_pset_new(1, _thread_pool_alloc , _thread_pool_free, ret)))
	    ERROR_LOG_GOTO(ERR, "Cannot create thread local pointer set as the lcoal thread pool");

	if(pthread_mutex_init(&ret->mutex, NULL) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the mutex for the memory pool");

	int i;
	for(i = 0; i < THREAD_NUM_TYPES; i ++)
	{
		ret->policy[i].cache_limit = _thread_object_max;
		ret->policy[i].alloc_unit = 1;
	}

	goto RET;
ERR:
	if(ret->local_pool != NULL) thread_pset_free(ret->local_pool);
	free(ret);
	ret = NULL;
RET:
	return ret;
}

int mempool_objpool_free(mempool_objpool_t* pool)
{
	int rc = 0;
	if(NULL == pool) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(pthread_mutex_destroy(&pool->mutex) < 0)
	{
		LOG_ERROR_ERRNO("Cannot dispose the pool mutex");
		rc = ERROR_CODE(int);
	}
	_page_t* current;
	for(;pool->pages;)
	{
		current = pool->pages;
		pool->pages = pool->pages->next;
		mempool_page_dealloc(current->page);
		free(current);
	}

	if(thread_pset_free(pool->local_pool) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot dispose the thread local memory pool");
		rc = ERROR_CODE(int);
	}

	free(pool);

	return rc;
}

__attribute__((noinline))
/**
 * @brief perform a global allocation from the global object memory pool
 * @note  this function will get min(alloc_unit, 2 * cache_limit - local_count) memory objects from the global pool.
 *        This means we we get at most alloc_unit memory objects, otherwise, we want to fill the local buffer as much
 *        as possible. <br/>
 *        However, if we used up all the cached object, then we allocate objects at most one page, because otherwise
 *        we actually waste our time on meaningless things.
 * @param pool the memory pool to allocate
 * @param tlp the thread local pool we want to store the result
 * @return the number of objects that has been allocated, or error code
 **/
static uint32_t _global_alloc(mempool_objpool_t* pool, _thread_local_pool_t* tlp)
{
	uint32_t ret = ERROR_CODE(uint32_t);

	if(pthread_mutex_lock(&pool->mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(uint32_t, "Cannot acquire the pool mutex");

	uint32_t to_alloc = _get_current_global_alloc_unit(pool);
	uint32_t cache_limit = _get_current_thread_cache_limit(pool);
	to_alloc = to_alloc > cache_limit ? cache_limit : to_alloc;

	_cached_object_t *begin = NULL, *end = NULL;
	uint32_t count = 0;

	if(NULL != pool->cached)
	{
		LOG_DEBUG("The global object pool has cached objects, reuse them first");

		end = begin = pool->cached;

		for(count = 1; count < to_alloc && end->next != NULL; count ++)
		{
			_cached_object_t* current = end;
			end = end->next;
			end->prev = current;
		}

		pool->cached = end->next;
		end->next = NULL;
		begin->prev = NULL;

		LOG_DEBUG("%u memory objects has been allocated from the global pool cache", count);
	}

	if(count < to_alloc)
	{
		LOG_DEBUG("The pool cache has been used up, but the local pool is still asking for %u more objects", to_alloc - count);
		if(NULL == pool->pages || _pagesize - pool->pages->unused_start < pool->obj_size)
		{
			LOG_DEBUG("The memory pool has no page for the new object, allocate a new page");
			_page_t* new_page = _page_new();
			if(NULL == new_page)
			    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate new page for the new allocation");

			new_page->next = pool->pages;
			pool->pages = new_page;
			pool->page_count ++;
			LOG_DEBUG("Allocated one more page in the object memory pool");
		}

		for(;count < to_alloc && _pagesize - pool->pages->unused_start >= pool->obj_size;)
		{
			_cached_object_t* new_obj = (_cached_object_t*)(((uint8_t*)pool->pages->page) + pool->pages->unused_start);
			pool->pages->unused_start += pool->obj_size;
			new_obj->next = NULL;
			new_obj->prev = end;
			if(begin == NULL)
			    begin = end = new_obj;
			else
			    end->next = new_obj, end = new_obj;
			count ++;
		}
		if(NULL != end) end->next = NULL;
	}

	tlp->begin = begin;
	tlp->end = end;
	ret = tlp->count = count;

	goto RET;
ERR:
	if(begin != NULL)
	{
		LOG_DEBUG("Allocation failure, release all the involved memory object to global pool");
		end->next = pool->cached;
		pool->cached = begin;
	}
RET:
	if(pthread_mutex_unlock(&pool->mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(uint32_t, "Cannot release the pool mutex");
	return ret;
}
/* Since OSX do not support this trick, we must disable this on darwin */
#if defined(FULL_OPTIMIZATION) && !defined(__DARWIN__)
__attribute__((weak, alias("_mempool_objpool_alloc_no_check")))
void* mempool_objpool_alloc(mempool_objpool_t* pool);
#endif
static void* _mempool_objpool_alloc_no_check(mempool_objpool_t* pool)
{
	void* ret = NULL;

	/* First try to get the object from the local pool */
	_thread_local_pool_t* tlp = thread_pset_acquire(pool->local_pool);

#ifdef FULL_OPTIMIZATION
	if(PREDICT_FALSE(NULL == tlp))
	    ERROR_PTR_RETURN_LOG("Cannot acquire the thread local pool for current thread TID=%u", thread_get_id());
#endif

	if(PREDICT_FALSE(tlp->count == 0))
	{
		if(PREDICT_FALSE(ERROR_CODE(uint32_t) == _global_alloc(pool, tlp)))
		    ERROR_PTR_RETURN_LOG("Cannot allocate memory from the global object memory pool");
	}
	else
	    LOG_DEBUG("The thread-local pool contains unused objects, reuse it");

	ret = tlp->begin;
	tlp->begin = tlp->begin->next;
	if(PREDICT_FALSE(tlp->begin == NULL))
	    tlp->end = tlp->exceeded = NULL;
	else if(tlp->exceeded != NULL)
	    tlp->exceeded = tlp->exceeded->next;

	tlp->count --;

	if(PREDICT_FALSE(NULL == ret))
	    LOG_ERROR("Cannot allocate memory from the object pool");

	return ret;
}
/* Since OSX do not support this trick, we must disable this on darwin */
#if defined(FULL_OPTIMIZATION) && !defined(__DARWIN__)
void* mempool_objpool_alloc_checked(mempool_objpool_t* pool)
#else
void* mempool_objpool_alloc(mempool_objpool_t* pool)
#endif
{
	if(PREDICT_FALSE(NULL == pool)) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(PREDICT_FALSE(_is_pool_disabled(pool)))
	    return malloc(pool->obj_size);
	return _mempool_objpool_alloc_no_check(pool);
}

int mempool_objpool_disabled(int val)
{
	_pool_disabled = val;

	return 0;
}

/**
 * @brief return a list of cached object to the global pool
 * @param begin the begin pointer of the list
 * @param end the end pointer of the list
 * @param pool the target memory pool
 * @return status code
 **/
static inline int _global_dealloc(mempool_objpool_t* pool, _cached_object_t* begin, _cached_object_t* end)
{
	if(pthread_mutex_lock(&pool->mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot acquire the pool mutex");

	end->next = pool->cached;
	if(NULL != pool->cached) pool->cached->prev = end;
	pool->cached = begin;
	begin->prev = NULL;

	if(pthread_mutex_unlock(&pool->mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot release the pool mutex");

	return 0;
}

__attribute__((noinline)) static int _do_global_dealloc(mempool_objpool_t* pool, _thread_local_pool_t* tlp, uint32_t cache_limit)
{
	_cached_object_t* new_end = tlp->exceeded->prev;
	if(PREDICT_FALSE(ERROR_CODE(int) == _global_dealloc(pool, tlp->exceeded, tlp->end)))
	    ERROR_RETURN_LOG(int, "Cannot deallocate the exceeded objects to the global pool");

	tlp->exceeded = NULL;
	tlp->end = new_end;
	tlp->end->next = NULL;
	tlp->count = cache_limit;

	return 0;
}
/* Since OSX do not support this trick, we must disable this on darwin */
#if defined(FULL_OPTIMIZATION) && !defined(__DARWIN__)
__attribute__((weak, alias("_mempool_objpool_dealloc_no_check")))
int mempool_objpool_dealloc(mempool_objpool_t* pool, void* mem);
#endif
static int _mempool_objpool_dealloc_no_check(mempool_objpool_t* pool, void* mem)
{

	_thread_local_pool_t* tlp = thread_pset_acquire(pool->local_pool);
#ifndef FULL_OPTIMIZATION
	if(PREDICT_FALSE(NULL == tlp))
	    ERROR_RETURN_LOG(int, "Cannot acquire the thread local pool for current thread TID=%u", thread_get_id());
#endif

	_cached_object_t* cur = (_cached_object_t*)mem;
	cur->prev = NULL;
	cur->next = tlp->begin;
	if(PREDICT_TRUE(tlp->begin != NULL)) tlp->begin->prev = cur;
	else tlp->end = cur;
	tlp->begin = cur;

	uint32_t cache_limit = _get_current_thread_cache_limit(pool);

	if(PREDICT_FALSE(tlp->count == cache_limit))
	    tlp->exceeded = tlp->end;
	else if(PREDICT_TRUE(tlp->count > cache_limit))
	    tlp->exceeded = tlp->exceeded->prev;

	tlp->count ++;

	if(PREDICT_FALSE(tlp->count > cache_limit * 2))
	    return _do_global_dealloc(pool, tlp, cache_limit);

	return 0;
}

/* Since OSX do not support this trick, we must disable this on darwin */
#if defined(FULL_OPTIMIZATION) && !defined(__DARWIN__)
int mempool_objpool_dealloc_checked(mempool_objpool_t* pool, void* mem)
#else
int mempool_objpool_dealloc(mempool_objpool_t* pool, void* mem)
#endif
{
	if(PREDICT_FALSE(NULL == pool || NULL == mem))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(PREDICT_FALSE(_is_pool_disabled(pool)))
	{
		free(mem);
		return 0;
	}

	return _mempool_objpool_dealloc_no_check(pool, mem);
}

uint32_t mempool_objpool_get_obj_size(const mempool_objpool_t* pool)
{
	if(NULL == pool) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");
	return pool->obj_size;
}

uint32_t mempool_objpool_get_page_count(const mempool_objpool_t* pool)
{
	if(NULL == pool) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");
	return pool->page_count;
}

int mempool_objpool_set_thread_policy(mempool_objpool_t* pool, unsigned thread_mask, mempool_objpool_tlp_policy_t policy)
{
	if(NULL == pool || policy.cache_limit < 1 || policy.alloc_unit == 0)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(pool->page_count > 0)
	    ERROR_RETURN_LOG(int, "Cannot change the thread policy of a object memory pool which is already in use");

	thread_type_t type_mask;
	uint32_t idx = 0;
	for(type_mask = 1; type_mask < THREAD_TYPE_MAX; type_mask <<= 1, idx ++)
	    if(thread_mask & type_mask)
	    {
#ifdef LOG_DEBUG_ENABLED
		    char thread_name_buf[32];
		    const char* thread_name = thread_type_name(type_mask, thread_name_buf, sizeof(thread_name_buf));
#endif /* LOG_DEBUG_ENABLED */
		    LOG_DEBUG("Set thread local memory pool policy for thread %s: <cache_limit = %u, alloc_unit = %u>",
		               thread_name, policy.cache_limit, policy.alloc_unit);
		    pool->policy[idx] = policy;
	    }

	return 0;
}
