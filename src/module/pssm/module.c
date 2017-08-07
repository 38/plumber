/**
 * Copyright (C) Hao Hou, 2017
 **/
/**
 * @todo split this file to smaller files
 **/
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

#include <utils/log.h>
#include <utils/mempool/objpool.h>
#include <utils/mempool/page.h>
#include <utils/thread.h>
#include <utils/static_assertion.h>

#include <itc/module_types.h>
#include <itc/module.h>

#include <error.h>
#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <sched/service.h>
#include <sched/rscope.h>
#include <sched/task.h>
#include <sched/step.h>

#include <module/pssm/module.h>

/**
 * @brief the actual data strcuture for an allocated memory chucnk
 **/
typedef struct {
	uint32_t size;            /*!< the size of the memory chunck, this is important, because this indicates which memory pool object should we use */
	uintptr_t __padding__[0];
	char     mem[0];          /*!< the actual start point of the memory chunck */
} _memory_chunck_t;
STATIC_ASSERTION_LAST(_memory_chunck_t, mem);
STATIC_ASSERTION_SIZE(_memory_chunck_t, mem, 0);

/**
 * @brief the data struct for a thread local data.
 * @details the reason for why we have this is, some of the servlets actually use the
 *         thread local for multithreading purpose.
 *         For example a javascript sevlet actually use the thread local to create
 *         interpreter isolations for each thread.
 *         This means, we can not allow the instance to be initialized unless someone try to get it.
 *         Which means we should allocate the new thread local memory when it was requested.
 *         (Because we don't want to create too many interpreter intances)
 *         However, this introduces a race condition: <br/>
 *         Consider thread A is current allocating memory, at the same time thread B is resizing the
 *         thread local data structure and copy the old pointers *before the thread A finish the allocation*
 *         This makes the memory allocated by thread A lost. <br/>
 *         In order to address this issue, we can not change the thread_pset code for this purpose. The only way
 *         for us to do that is lazy initialization, which is inside the PSSM module, when the thread_pset ask for
 *         new pointer creation, we only create a pointer points to the object that servlet want, which is currently NULL.
 *         And when the servlet calls thread_local_get, and we get the pointer to the object, if it's NULL then we
 *         call the servlet's allocator function.<br/>
 *         In this way we can avoid this race condition
 **/
typedef struct {
	thread_pset_allocate_t   allocator;   /*!< the allocator */
	thread_pset_deallocate_t deallocator; /*!< the deallocator */
	void*                    data;        /*!< the additional data for the inner allocator */
} _thread_local_data_t;

/**
 * @brief represents an object that is allocated for only one thread (See the detailed documentation for _thread_local_data_t for details)
 **/
typedef struct {
	uint32_t tid;   /*!< the thread id that this object belongs to */
	void* object;   /*!< the actuall object */
} _thread_local_object_t;

/**
 * @brief indicates if this module has been initialized
 **/
static int _initialized = 0;

/**
 * @brief the memory pools that allocate the memory chuncks in different sizes
 **/
static mempool_objpool_t** _pools;

/**
 * @brief the limit of the maximum size the pool can allocate
 **/
static uint32_t _size_limit = 4096;

/**
 * @brief the mutex use to seralize the mempool creation operation
 **/
static pthread_mutex_t _mempool_mutex;

/**
 * @brief the actuall callback function that should be called when
 *        the plumber server is exited
 * @param data the additional data to pass in (note the function is also responsible for disposing the data
 * @return nothing
 **/
typedef void (*_on_exit_callback_t)(void* data);

/**
 * @brief the data structure for an on exit callback
 **/
typedef struct _on_exit_t {
	void*               data;   /*!< the data for the callback */
	struct _on_exit_t*  next;   /*!< the next pointer in the linked list */
	_on_exit_callback_t func;   /*!< the actual callback function */
} _on_exit_t;

/**
 * @brief the on exit list mutex
 **/
pthread_mutex_t _on_exit_mutex;

/**
 * @brief the on exit list
 **/
static _on_exit_t* _on_exit_list = NULL;

static int _init(void* __restrict context, uint32_t argc, char const* __restrict const* __restrict argv)
{
	if(_initialized) ERROR_RETURN_LOG(int, "cannot initialize the singleton module twice");
	(void)context;
	(void)argc;
	(void)argv;

	/* Intialize the memory pool */
	if(pthread_mutex_init(&_mempool_mutex, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the mempool module mutex");

	_pools = (mempool_objpool_t**)calloc(_size_limit, sizeof(mempool_objpool_t**));
	if(NULL == _pools)
	{
		pthread_mutex_destroy(&_mempool_mutex);
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the object pool array");
	}

	/* Initialize the on exit list */
	if(pthread_mutex_init(&_on_exit_mutex, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the mutex for the on exit list");

	_initialized = 1;
	return 0;
}
static inline int _onexit(void* __restrict context)
{
	(void)context;

	int rc = 0;

	/* Call the callbacks */
	_on_exit_t* ptr;
	for(ptr = _on_exit_list; NULL != ptr;)
	{
		_on_exit_t* cur = ptr;
		ptr = ptr->next;
		if(NULL != cur->func)
		    cur->func(cur->data);
		free(cur);
	}

	return rc;
}

static int _cleanup(void* __restrict context)
{
	(void)context;

	int rc = 0;

	uint32_t i;
	for(i = 0; i < _size_limit; i ++)
	    if(_pools[i] != NULL && mempool_objpool_free(_pools[i]) == ERROR_CODE(int))
	        rc = ERROR_CODE(int);

	free(_pools);

	if(pthread_mutex_destroy(&_mempool_mutex) < 0)
	    rc = ERROR_CODE(int);

	if(pthread_mutex_destroy(&_on_exit_mutex) < 0)
	    rc = ERROR_CODE(int);

	_initialized = 0;

	return rc;
}

/**
 * @brief get the memory pool object for the given size
 * @param size the size of the memory chunck
 * @return the memory pool that is used for memory chuncks in this size
 **/
static inline mempool_objpool_t* _get_pool(uint32_t size)
{
	if(size == 0) return NULL;
	if(size > _size_limit) ERROR_PTR_RETURN_LOG("The memory chunck size limit exceeded");

	uint32_t idx = size - 1;   /* Because actually we do not allow the 0 sized allocation, so the offset should be size - 1 */

	if(_pools[idx] != NULL) return _pools[idx];

	if(pthread_mutex_lock(&_mempool_mutex) < 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot acquire the mempool module global mutex");

	if(_pools[idx] == NULL && NULL == (_pools[idx] = mempool_objpool_new(size + (uint32_t)sizeof(_memory_chunck_t))))
	{
		if(pthread_mutex_unlock(&_mempool_mutex) < 0)
		    LOG_WARNING("Cannot release the mempool module global mutex");
		ERROR_PTR_RETURN_LOG("Cannot allocate new memory pool for the size %u", size);
	}
	if(pthread_mutex_unlock(&_mempool_mutex) < 0)
	    LOG_WARNING("Cannot release the mempool module global mutex");

	return _pools[idx];
}

static const char* _get_path(void* __restrict ctx, char* buf, size_t sz)
{
	(void)ctx;
	(void)sz;
	buf[0] = 0;
	return buf;
}

/**
 * @brief allocate a memory chunck of given size from mempool
 * @param size the size requested
 * @param retbuf the return buffer
 * @return status code
 **/
static inline int _pool_alloc(uint32_t size, void** retbuf)
{
	if(NULL == retbuf) ERROR_RETURN_LOG(int, "Invalid arguments");

	mempool_objpool_t* pool = _get_pool(size);
	if(NULL == pool) ERROR_RETURN_LOG(int, "Cannot get the memory pool");

	_memory_chunck_t* chunck;
	if(NULL == (chunck = (_memory_chunck_t*)mempool_objpool_alloc(pool)))
	    ERROR_RETURN_LOG(int, "Cannot allocate memory from the memory pool");

	chunck->size = size;
	(*retbuf) = (void*)chunck->mem;
	return 0;
}

static inline int _pool_dealloc(void* mem)
{
	if(NULL == mem) ERROR_RETURN_LOG(int, "Invalid arguments");

	_memory_chunck_t* chunck = (_memory_chunck_t*)&((char*)mem)[-sizeof(_memory_chunck_t)];

	mempool_objpool_t* pool = _get_pool(chunck->size);
	if(NULL == pool) ERROR_RETURN_LOG(int, "Cannot get the memory pool");

	return mempool_objpool_dealloc(pool, chunck);
}

static inline void* _thread_local_allocator(uint32_t tid, const void* data)
{
	if(NULL == data || tid == ERROR_CODE(uint32_t)) ERROR_PTR_RETURN_LOG("Invalid arguments");

	/*TODO: use the mempool */
	_thread_local_object_t* ret = (_thread_local_object_t*)malloc(sizeof(_thread_local_object_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allcoate memory for the thread local object");

	ret->tid = tid;
	ret->object = NULL;

	return ret;
}

static inline int _thread_local_deallocator(void* mem, const void* data)
{
	if(NULL == mem || NULL == data) ERROR_RETURN_LOG(int, "Invalid arguments");

	_thread_local_data_t* tld = (_thread_local_data_t*)data;

	if(tld->deallocator == NULL) ERROR_RETURN_LOG(int, "Invalid thread local data object");

	_thread_local_object_t* obj = (_thread_local_object_t*)mem;

	int rc = obj->object != NULL ? tld->deallocator(obj->object, tld->data) : 0;

	free(mem);
	return rc;
}

static inline int _thread_local_new(thread_pset_allocate_t alloc, thread_pset_deallocate_t dealloc, void* data, thread_pset_t** retbuf)
{
	if(NULL == alloc || NULL == dealloc || NULL == retbuf) ERROR_RETURN_LOG(int, "Invalid arguments");

	/* TODO use the memory pool */
	_thread_local_data_t* tld = (_thread_local_data_t*)malloc(sizeof(_thread_local_data_t));
	if(NULL == tld) ERROR_RETURN_LOG_ERRNO(int, "Cannot allcoate memory for the thread local data object");
	tld->allocator = alloc;
	tld->deallocator = dealloc;
	tld->data = data;

	/* TODO remove magic number */
	*retbuf = thread_pset_new(8, _thread_local_allocator, _thread_local_deallocator, tld);
	if(*retbuf == NULL) return ERROR_CODE(int);

	return 0;
}

static inline int _thread_local_get(thread_pset_t* pset, void** retbuf)
{
	if(NULL == pset || NULL == retbuf) ERROR_RETURN_LOG(int, "Invalid arguments");

	_thread_local_data_t* tld = (_thread_local_data_t*)thread_pset_get_callback_data(pset);
	if(tld == NULL) ERROR_RETURN_LOG(int, "Cannot get the thread local data");
	_thread_local_object_t* obj = (_thread_local_object_t*)thread_pset_acquire(pset);
	if(NULL == obj) ERROR_RETURN_LOG(int, "Cannot acquire the thread local object");

	if(NULL == obj->object && NULL == (obj->object = tld->allocator(obj->tid, tld->data)))
	    ERROR_RETURN_LOG(int, "Cannot create new object for the thread local");

	if(NULL == (*retbuf = obj->object))
	    return ERROR_CODE(int);

	return 0;
}

static inline int _thread_local_free(thread_pset_t* pset)
{
	if(NULL == pset) ERROR_RETURN_LOG(int, "Invalid arguments");


	void* mem = (void*)thread_pset_get_callback_data(pset);

	int rc = thread_pset_free(pset);

	if(NULL != mem) free(mem);

	return rc;
}

/**
 * @note The external token used by the application is shifted by 1 compare to the internal one.
 * Yes, this is a tricky solution, however, it's make sense that we have 0 in the 
 * internal representation of RLS token as a valid token. Because the array is 0
 * based. 
 * But in the application level, this is not true, because the libproto writes 0 
 * by default. So to avoid it getting wrong RLS object, we should disallow this from
 * happening, which means RLS token 0 shouldn't be a valid token.
 * This solution actually addresses this.
 * @todo Any better solution than have this hack ?
 **/
static inline int _rscope_add(const runtime_api_scope_entity_t* entity, uint32_t* result)
{
	sched_rscope_t* current = sched_step_current_scope();
	if(NULL == current)
	    ERROR_RETURN_LOG(int, "Cannot get the current scope");

	if(NULL == entity || NULL == result)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t internal_token;

	if(ERROR_CODE(uint32_t) == (internal_token = sched_rscope_add(current, entity)))
	    ERROR_RETURN_LOG(int, "Cannot add pointer to scope");

	*result = internal_token + 1;

	return 0;
}

/**
 * @note see the notes for _rscope_add, the same situation applies
 **/
static inline int _rscope_copy(uint32_t token, uint32_t* result_token, void** result_ptr)
{
	sched_rscope_t* current = sched_step_current_scope();
	if(NULL == current)
	    ERROR_RETURN_LOG(int, "Cannot get the current scope");

	if(token == 0 || token == ERROR_CODE(uint32_t)) 
		ERROR_RETURN_LOG(int, "Invalid arguments: application level RLS token is invalid");

	runtime_api_scope_token_t internal_token = token - 1;

	sched_rscope_copy_result_t result;
	int rc = sched_rscope_copy(current, internal_token, &result);

	if(rc != ERROR_CODE(int))
	{
		if(NULL != result_token) *result_token = result.token + 1;
		if(NULL != result_ptr) *result_ptr = result.ptr;
	}

	return rc;
}

/**
 * @note see the notes for _rscope_add
 **/
static inline int _rscope_get(uint32_t token, const void** result)
{
	if(NULL == result)
	    ERROR_RETURN_LOG(int, "Invalid arguments");
	
	if(token == 0 || token == ERROR_CODE(uint32_t)) 
		ERROR_RETURN_LOG(int, "Invalid arguments: application level RLS token is invalid");

	runtime_api_scope_token_t internal_token = token - 1;


	sched_rscope_t* current = sched_step_current_scope();
	if(NULL == current)
	    ERROR_RETURN_LOG(int, "Cannot get the current scope");
	
	*result = sched_rscope_get(current, internal_token);

	if(NULL == *result) return ERROR_CODE(int);

	return 0;
}

static inline int _add_on_exit_callback(_on_exit_callback_t callback, void* data)
{
	if(NULL == callback)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_on_exit_t* obj = (_on_exit_t*)malloc(sizeof(*obj));
	if(NULL == obj)
	    ERROR_RETURN_LOG(int, "Cannot allocate memory for the new on exit object");

	obj->func = callback;
	obj->data = data;

	if(pthread_mutex_lock(&_on_exit_mutex) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot acquire the on exit list mutex");

	/* This is actually a stack */
	obj->next = _on_exit_list;
	_on_exit_list = obj;

	if(pthread_mutex_unlock(&_on_exit_mutex) < 0)
	    LOG_WARNING("Cannot release the on exit list mutex");

	return 0;

ERR:
	free(obj);
	return ERROR_CODE(int);
}

static inline int _page_allocate(void** resbuf)
{
	if(NULL == resbuf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL == (*resbuf = mempool_page_alloc()))
	    ERROR_RETURN_LOG(int, "Cannot allocate page from the page memory pool");

	return 0;
}

static inline int _page_deallocate(void* page)
{
	if(NULL == page)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return mempool_page_dealloc(page);
}


static int _invoke(void* __restrict ctx, uint32_t opcode, va_list args)
{
	(void)ctx;
	if(_initialized == 0) ERROR_RETURN_LOG(int, "Bug: The module haven't been initialized");


	uint32_t size;
	void**   retbuf;
	void*    ptr;

	thread_pset_allocate_t alloc;
	thread_pset_deallocate_t dealloc;
	void* data;
	thread_pset_t* pset;
	thread_pset_t** pset_ret;
	switch(opcode)
	{
		case MODULE_PSSM_MODULE_OPCODE_POOL_ALLOCATE:
		    size = va_arg(args, uint32_t);
		    retbuf = va_arg(args, void**);
		    return _pool_alloc(size, retbuf);
		case MODULE_PSSM_MODULE_OPCODE_POOL_DEALLOCATE:
		    ptr = va_arg(args, void*);
		    return _pool_dealloc(ptr);
		case MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_NEW:
		    alloc = va_arg(args, thread_pset_allocate_t);
		    dealloc = va_arg(args, thread_pset_deallocate_t);
		    data = va_arg(args, void*);
		    pset_ret = va_arg(args, thread_pset_t**);
		    return _thread_local_new(alloc, dealloc, data, pset_ret);
		case MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_GET:
		    pset = va_arg(args, thread_pset_t*);
		    retbuf = va_arg(args, void**);
		    return _thread_local_get(pset, retbuf);
		case MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_FREE:
		    pset = va_arg(args, thread_pset_t*);
		    return _thread_local_free(pset);
		case MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_ADD:
		{
			const runtime_api_scope_entity_t* ent = va_arg(args, const runtime_api_scope_entity_t*);
			uint32_t* result = va_arg(args, uint32_t*);
			return _rscope_add(ent, result);
		}
		case MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_COPY:
		{
			uint32_t token = va_arg(args, uint32_t);
			uint32_t* token_buf = va_arg(args, uint32_t*);
			void** ptr_buf = va_arg(args, void**);
			return _rscope_copy(token, token_buf, ptr_buf);
		}
		case MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_GET:
		{
			uint32_t token = va_arg(args, uint32_t);
			const void** ptr_buf = va_arg(args, const void**);
			return _rscope_get(token, ptr_buf);
		}
		case MODULE_PSSM_MODULE_OPCODE_ON_EXIT:
		{
			_on_exit_callback_t callback = va_arg(args, _on_exit_callback_t);
			void* data = va_arg(args, void*);
			return _add_on_exit_callback(callback, data);
		}
		case MODULE_PSSM_MODULE_OPCODE_PAGE_ALLOCATE:
		{
			void** result_buf = va_arg(args, void**);
			return _page_allocate(result_buf);
		}
		case MODULE_PSSM_MODULE_OPCODE_PAGE_DEALLOCATE:
		{
			void* page = va_arg(args, void*);
			return _page_deallocate(page);
		}
		default:
		    ERROR_RETURN_LOG(int, "Invalid opcode 0x%x", opcode);
	}
}

static uint32_t _get_opcode(void* __restrict ctx, const char* name)
{
	(void)ctx;
	if(NULL == name) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");
	if(strcmp(name, "pool_allocate") == 0) return MODULE_PSSM_MODULE_OPCODE_POOL_ALLOCATE;
	if(strcmp(name, "pool_deallocate") == 0) return MODULE_PSSM_MODULE_OPCODE_POOL_DEALLOCATE;
	if(strcmp(name, "thread_local_new") == 0) return MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_NEW;
	if(strcmp(name, "thread_local_get") == 0) return MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_GET;
	if(strcmp(name, "thread_local_free") == 0) return MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_FREE;
	if(strcmp(name, "scope_add") == 0) return MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_ADD;
	if(strcmp(name, "scope_copy") == 0) return MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_COPY;
	if(strcmp(name, "scope_get") == 0) return MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_GET;
	if(strcmp(name, "on_exit") == 0) return MODULE_PSSM_MODULE_OPCODE_ON_EXIT;
	if(strcmp(name, "page_allocate") == 0) return MODULE_PSSM_MODULE_OPCODE_PAGE_ALLOCATE;
	if(strcmp(name, "page_deallocate") == 0) return MODULE_PSSM_MODULE_OPCODE_PAGE_DEALLOCATE;

	ERROR_RETURN_LOG(uint32_t, "Invalid method name %s", name);
}

itc_module_t module_pssm_module_def = {
	.mod_prefix     = "plumber.std",
	.context_size   = 0,    /*!< no context */
	.handle_size    = 0,    /*!< no handle */
	.module_init    = _init,
	.module_cleanup = _cleanup,
	.get_path       = _get_path,
	.invoke         = _invoke,
	.get_opcode     = _get_opcode,
	.on_exit        = _onexit
};
