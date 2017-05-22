/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <barrier.h>
#include <errno.h>

#include <pthread.h>

#include <error.h>
#include <arch/arch.h>
#include <utils/mempool/page.h>
#include <utils/log.h>

extern void  __libc_free(void* ptr);
/**
 * @brief represents a unused page
 **/
typedef struct _page_t {
	struct _page_t* prev;  /*!< the prev free page */
	struct _page_t* next;  /*!< the next free page */
} _page_t;

/**
 * @brief the free page list
 **/
static _page_t* _free_list;

/**
 * @brief the number of free pages
 **/
static size_t _num_free_pages;

/**
 * @brief the max number of the thread pages
 * @note the default limit is 512M memory, 0x20000 pages
 **/
static size_t _max_cached_pages = 0x20000;

/**
 * @brief the max number of pages a thread can hold (4k * 1k = 4Mb)
 **/
static size_t _max_thread_cached_pages = 0x1000;

/**
 * @brief a thread page pool
 **/
typedef struct _thread_page_pool_t{
	uint32_t page_count;      /*!< how many pages in the thread pool */
	_page_t* page_list_begin; /*!< the first page in the free list */
	_page_t* exceeded;        /*!< the first element that exceeded the thread pool size limit */
	_page_t* page_list_end;   /*!< the last element */
	struct _thread_page_pool_t* next; /*!< the next thread page pool */
} _thread_page_pool_t;

static __thread _thread_page_pool_t* _local_page_pool = NULL;

static _thread_page_pool_t* _local_page_pool_list = NULL;

static int _pool_disabled = 0;

pthread_mutex_t mutex;

int mempool_page_init()
{
	_free_list = NULL;
	_num_free_pages = 0;
	return 0;
}

int mempool_page_finalize()
{
	_page_t* tmp;
	for(;_free_list != NULL;)
	{
		tmp = _free_list;
		_free_list = _free_list->next;
		__libc_free(tmp);
	}

	_thread_page_pool_t* curpool;
	for(;NULL != _local_page_pool_list;)
	{
		curpool = _local_page_pool_list;
		_local_page_pool_list = _local_page_pool_list->next;
		_page_t* curpage;
		for(;curpool->page_list_begin != NULL;)
		{
			curpage = curpool->page_list_begin;
			curpool->page_list_begin = curpool->page_list_begin->next;
			__libc_free(curpage);
		}
		free(curpool);
	}

	return 0;
}


int mempool_page_set_free_page_limit(size_t npages)
{
	arch_atomic_sw_assignment_sz(&_max_cached_pages, npages);
	return 0;
}

/**
 * @brief get the size of each page
 **/
static inline size_t _get_page_size()
{
	int ret = getpagesize();
	if(ret < 0) return 0;
	return (size_t) ret;
}

/**
 * @brief allocate n contiguous pages
 * @param n the number of pages to allocate
 * @return the allocate memory
 **/
static inline void* _page_alloc(int n)
{
	void* ret = NULL;
	if(posix_memalign(&ret, _get_page_size(), _get_page_size() * (size_t)n) < 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate a full page");

	return ret;
}

_page_t* _global_alloc()
{
	_page_t* claimed = NULL;

	/* try to claim one */
	for(;_free_list;)
	{
		_page_t* candidate = _free_list;
		_page_t* new_head = candidate->next;

		BARRIER();

		if(__sync_bool_compare_and_swap(&_free_list, candidate, new_head))
		{
			claimed = candidate;
			break;
		}
	}

	if(NULL == claimed)
	{
		LOG_DEBUG("the page pool do not have page for current allocation, ask the system for a new one");
		return (_page_t*)_page_alloc(1);
	}
	else LOG_DEBUG("Use cached page %p", claimed);

	return claimed;
}

int _global_dealloc(_page_t* begin, _page_t* end, size_t n)
{
	for(;begin != NULL && _max_cached_pages <= _num_free_pages + n;)
	{
		LOG_DEBUG("The number of free pages is larger than the free page limit, free the page directly");
		_page_t* tmp = begin;
		begin = begin->next;
		__libc_free(tmp);
		n--;
	}

	if(begin == NULL) return 0;

	__sync_fetch_and_add(&_num_free_pages, n);
	BARRIER();
	for(;;)
	{
		_page_t* old_head = _free_list;
		end->next = old_head;

		BARRIER();

		if(__sync_bool_compare_and_swap(&_free_list, old_head, begin))
		    break;
	}

	LOG_DEBUG("%zu pages has been return to the global pool", n);

	return 0;
}
static inline int _check_local_pool()
{
	if(NULL == _local_page_pool)
	{
		_local_page_pool = (_thread_page_pool_t*)malloc(sizeof(_thread_page_pool_t));
		if(NULL == _local_page_pool)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate the thread local page pool");
		_local_page_pool->page_list_begin = _local_page_pool->page_list_end = _local_page_pool->exceeded = NULL;
		_local_page_pool->page_count = 0;
		_local_page_pool->next = _local_page_pool_list;
		_local_page_pool_list = _local_page_pool;
		LOG_DEBUG("Thread local page pool has been initialized");
	}

	return 0;
}
void* mempool_page_alloc()
{
	if(_pool_disabled)
	{
		return malloc(_get_page_size());
	}

	if(_check_local_pool() == ERROR_CODE(int))
	    ERROR_PTR_RETURN_LOG("cannot initialize the local pool");

	_page_t* ret = NULL;

	if(_local_page_pool->page_list_begin != NULL)
	{
		LOG_DEBUG("Reuse the cached page");
		ret = _local_page_pool->page_list_begin;
		_local_page_pool->page_list_begin = _local_page_pool->page_list_begin->next;
		_local_page_pool->page_count --;
		if(_local_page_pool->exceeded != NULL)
		    _local_page_pool->exceeded = _local_page_pool->exceeded->next;
		if(_local_page_pool->page_list_begin != NULL)
		    _local_page_pool->page_list_begin->prev = NULL;
		else
		    _local_page_pool->page_list_end = NULL;
	}

	if(NULL == ret) return _global_alloc();

	return ret;
}

int mempool_page_dealloc(void* mem)
{
	if(_pool_disabled)
	{
		free(mem);
		return 0;
	}

	if(_check_local_pool() == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "cannot initialize the local pool");

	_page_t* page = (_page_t*)mem;

	page->next = _local_page_pool->page_list_begin;
	page->prev = NULL;
	if(_local_page_pool->page_list_begin != NULL)
	    _local_page_pool->page_list_begin->prev = page;
	_local_page_pool->page_list_begin = page;
	if(_local_page_pool->page_list_end == NULL) _local_page_pool->page_list_end = page;


	if(_local_page_pool->page_count ++ == _max_thread_cached_pages)
	    _local_page_pool->exceeded = _local_page_pool->page_list_end;
	else if(_local_page_pool->exceeded != NULL)
	    _local_page_pool->exceeded = _local_page_pool->exceeded->prev;

	int rc = 0;

	if(_local_page_pool->page_count >= _max_thread_cached_pages * 2)
	{
		/* Because the exceeded page may be disposed, so make sure we saved the value first */
		_page_t* end = _local_page_pool->exceeded->prev;

		rc = _global_dealloc(_local_page_pool->exceeded, _local_page_pool->page_list_end, _local_page_pool->page_count - _max_thread_cached_pages);
		_local_page_pool->page_list_end = end;
		_local_page_pool->page_list_end->next = NULL;

		_local_page_pool->exceeded = NULL;
		_local_page_pool->page_count = (uint32_t)_max_thread_cached_pages;
	}


	return rc;
}

void mempool_page_disable(int val)
{
	_pool_disabled = val;
}
