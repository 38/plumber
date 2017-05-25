/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/prctl.h>

#include <error.h>
#include <predict.h>
#include <utils/log.h>
#include <utils/thread.h>
#include <utils/string.h>
#include <utils/static_assertion.h>

/**
 * @brief the human readable thread type name
 **/
static const char* _type_name[] = {
	"EventLoopThread",
	"DispatcherThread",
	"WorkerThread",
	"AsyncIOThread",
	NULL
};

/**
 * @brief the actual pointer array used to store the pointers
 **/
typedef struct _pointer_array_t {
	struct _pointer_array_t* unused; /*!< currently unused pointer array */
	uint32_t size;                   /*!< the size of the pointer */
	uintptr_t __padding__[0];
	void*    ptr[0];                 /*!< the actual pointers for each thread */
} _pointer_array_t;
STATIC_ASSERTION_LAST(_pointer_array_t, ptr);
STATIC_ASSERTION_SIZE(_pointer_array_t, ptr, 0);

/**
 * @brief the actual data structure for the thread local pointer set
 **/
struct _thread_pset_t {
	pthread_mutex_t resize_lock;  /*!< the resize lock for this pointer set */
	const void*     data;         /*!< the additional data passed to the allocator/deallocator */
	thread_pset_allocate_t alloc; /*!< the allocation function */
	thread_pset_deallocate_t dealloc; /*!< the deallocation function */
	_pointer_array_t* array;      /*!< the actual pointer array */
};

/**
 * @brief the represent a cleanup hook
 **/
typedef struct _cleanup_hook_t {
	thread_cleanup_t func;   /*!< the function */
	void*            arg;    /*!< the argument */
	struct _cleanup_hook_t* next;   /*!< the next callback function */
} _cleanup_hook_t;

#ifdef STACK_SIZE
/**
 * @brief The thread local storage
 **/
typedef struct {
	uint32_t  id;              /*!< The thread id */
	thread_t* thread;          /*!< The thread object */
	uintptr_t  __padding__[0];
	char base[0];             /*!< The base address of the stack */
} _stack_t;
#endif

/**
 * @brief the actual data structure for a thread object
 * @note the cleanup hook is actually a stack, the latest added function will be executed first
 **/
struct _thread_t {
	pthread_t     handle;      /*!< the pthread handle */
	thread_main_t main;        /*!< the thread main function */
	void*         arg;         /*!< the thread argument */
	_cleanup_hook_t* hooks;    /*!< the cleanup hooks */
	thread_type_t type;        /*!< the type of this thread */
#ifdef STACK_SIZE
	char          mem[STACK_SIZE * 2 + sizeof(_stack_t)]; /*!< The memory used for task */
	_stack_t*     stack;       /*!< The stack we need to use */
#endif
};

#ifndef STACK_SIZE
/**
 * @brief indicates which thread is it
 **/
static __thread uint32_t _thread_id = ERROR_CODE(uint32_t);

/**
 * @brief current thread object
 **/
static __thread thread_t* _thread_obj = NULL;
#endif

/**
 * @brief used to assign an untagged thread a thread id
 **/
static uint32_t _next_thread_id = 0;

#ifdef STACK_SIZE
/**
 * @brief Get the current stack object
 * @return The pointer of current stack
 * @note This only works with the thread created by thread_new
 **/
static inline _stack_t* _get_current_stack()
{
	uintptr_t addr = (uintptr_t)&addr;
	addr = addr - addr % STACK_SIZE - sizeof(_stack_t);
	return (_stack_t*)addr;
}
#endif

/**
 * @brief get the thread id of current thread
 * @return the thread id
 **/
static inline uint32_t _get_thread_id()
{
#ifdef STACK_SIZE
	return _get_current_stack()->id;
#else
	if(PREDICT_FALSE(_thread_id == ERROR_CODE(uint32_t)))
	{
		uint32_t claimed_id;
		do {
			claimed_id = _next_thread_id;
		} while(!__sync_bool_compare_and_swap(&_next_thread_id, claimed_id, claimed_id + 1));

		_thread_id = claimed_id;

		LOG_DEBUG("Assign new thread ID %u to thread", claimed_id);
	}
	return _thread_id;
#endif
}
/**
 * @brief Allocate the pointer for current thread
 * @note  Because GCC always wants to inline anything if possible on -O3,
 *        However, this inline is harmful, because the allocation will only
 *        happen limited times. So this inline causes the function needs to
 *        save more registers than it actally needs
 * @param pset The pointer set
 * @param tid  The thread id
 * @return pointer has been allocated
 **/
__attribute__((noinline)) static void* _allocate_current_pointer(thread_pset_t* pset, uint32_t tid)
{
	if(pthread_mutex_lock(&pset->resize_lock) < 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot acquire the resize lock");

	/* Then the thread should be the only one executing this code,
	 * At the same time, we need to check again, in case the pointer
	 * has been created during the time of the thread being blocked */
	_pointer_array_t* current = pset->array;
	if(current->size > tid)
	{
		if(pthread_mutex_unlock(&pset->resize_lock) < 0)
		    LOG_WARNING_ERRNO("Cannot release the resize lock");
		return current->ptr[tid];
	}

	/* Otherwise, it means we need to create more cells in the array */
	uint32_t new_size = current->size;
	for(;new_size <= tid; new_size *= 2);
	LOG_TRACE("Resizing the pointer array from size %u to %u", current->size, new_size);

	_pointer_array_t* new_array = (_pointer_array_t*)malloc(new_size * sizeof(void*) + sizeof(_pointer_array_t));
	uint32_t i = 1;
	if(NULL == new_array)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the new array");

	new_array->size = new_size;

	/*Then copy the previous pointers to new one */
	memcpy(new_array->ptr, current->ptr, current->size * sizeof(void*));

	/* Then allocate new pointers for the new threads */
	for(i = current->size; i < new_size; i ++)
	{
		if(NULL == (new_array->ptr[i] = pset->alloc(i, pset->data)))
		    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the new pointer");
	}

	new_array->unused = current;

	pset->array = new_array;

	if(pthread_mutex_unlock(&pset->resize_lock) < 0)
	    LOG_WARNING_ERRNO("Cannot release the resize lock");

	return pset->array->ptr[tid];
ERR:
	if(pthread_mutex_unlock(&pset->resize_lock) < 0)
	    LOG_WARNING_ERRNO("Cannot release the resize lock");

	if(new_array != NULL)
	{
		for(; i >= current->size; i --)
		    if(new_array->ptr[i] != NULL)
		        pset->dealloc(new_array->ptr[i], pset->data);
		free(new_array);
	}

	return NULL;
}

static inline void* _get_current_pointer(thread_pset_t* pset)
{
	uint32_t tid = _get_thread_id();

	_pointer_array_t* current = pset->array;

	/* If the pointer for the thread is already there */
	if(PREDICT_TRUE(current->size > tid))
	    return current->ptr[tid];

	return _allocate_current_pointer(pset, tid);
}

thread_pset_t* thread_pset_new(uint32_t init_size, thread_pset_allocate_t alloc, thread_pset_deallocate_t dealloc, const void* data)
{
	if(NULL == alloc || NULL == dealloc || init_size == 0 || init_size == ERROR_CODE(uint32_t))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	uint32_t mutex_init = 0;
	uint32_t i = 0;
	thread_pset_t* ret = (thread_pset_t*)malloc(sizeof(thread_pset_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the thread pointer set object");

	ret->array = NULL;

	if(pthread_mutex_init(&ret->resize_lock, NULL) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the resize lock");

	ret->alloc = alloc;
	ret->dealloc = dealloc;

	if(NULL == (ret->array = (_pointer_array_t*)calloc(1, sizeof(_pointer_array_t) + sizeof(void*) * init_size)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the poitner array");

	for(i = 0; i < init_size; i ++)
	    if(NULL == (ret->array->ptr[i] = alloc(i, data)))
	        ERROR_LOG_GOTO(ERR, "Cannot create new pointer for thread %u", i);

	ret->array->size = init_size;
	ret->data = data;

	return ret;
ERR:
	if(mutex_init && pthread_mutex_unlock(&ret->resize_lock) < 0)
	    LOG_WARNING_ERRNO("Cannot dispose the resize lock");
	if(NULL != ret->array)
	{
		uint32_t j;
		for(j = 0; j < i; j ++)
		    dealloc(ret->array->ptr[j], data);
		free(ret->array);
	}
	free(ret);
	return NULL;
}

int thread_pset_free(thread_pset_t* pset)
{
	if(NULL == pset) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;

	if(pthread_mutex_destroy(&pset->resize_lock) < 0)
	{
		LOG_WARNING_ERRNO("Cannot dispose the resize lock");
		rc = ERROR_CODE(int);
	}

	uint32_t i;
	for(i = 0; i < pset->array->size; i ++)
	    if(pset->dealloc(pset->array->ptr[i], pset->data) == ERROR_CODE(int))
	    {
		    LOG_WARNING("Cannot dispose the pointer for thread %u", i);
		    rc = ERROR_CODE(int);
	    }

	_pointer_array_t* ptr, *tmp;
	for(ptr = pset->array; ptr != NULL; )
	{
		tmp = ptr;
		ptr = ptr->unused;
		free(tmp);
	}

	free(pset);

	return rc;
}

void* thread_pset_acquire(thread_pset_t* pset)
{
#ifndef FULL_OPTIMIZATION
	if(PREDICT_FALSE(NULL == pset))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");
#endif
	return _get_current_pointer(pset);
}

const void* thread_pset_get_callback_data(thread_pset_t* pset)
{
	if(NULL == pset) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return pset->data;
}

uint32_t thread_get_id()
{
	return _get_thread_id();
}

static void* _thread_main(void* data)
{
#ifdef STACK_SIZE
	_stack_t* stack = _get_current_stack();

	do {
		stack->id = _next_thread_id;
	} while(!__sync_bool_compare_and_swap(&_next_thread_id, stack->id, stack->id + 1));
#endif

	thread_t* thread = (thread_t*)data;
	void* ret = thread->main(thread->arg);

	_cleanup_hook_t* ptr;
	for(ptr = thread->hooks; NULL != ptr; ptr = ptr->next)
	    if(ERROR_CODE(int) == ptr->func(thread->arg, ptr->arg))
	        LOG_WARNING("Thread cleanup function <func = %p, arg = %p> returned with an error", ptr->func, ptr->arg);

	return ret;
}

#ifdef STACK_SIZE
static void* _start_main(void *ctx)
{
	_get_current_stack()->id = 0;
	_next_thread_id = 1;
	thread_test_main_t func = (thread_test_main_t)ctx;;
	if(func() == 0) return ctx;
	return NULL;
}
#endif

int thread_run_test_main(thread_test_main_t func)
{
#ifdef STACK_SIZE
	thread_t* ret = (thread_t*)malloc(sizeof(thread_t));
	if(NULL == ret) return -1;

	uintptr_t offset = (STACK_SIZE - ((uintptr_t)ret->mem) % STACK_SIZE) % STACK_SIZE;
	if(offset >= sizeof(_stack_t))
	    ret->stack = (_stack_t*)(ret->mem + offset - sizeof(_stack_t));
	else
	    ret->stack = (_stack_t*)(ret->mem + offset + STACK_SIZE - sizeof(_stack_t));

	size_t page_size = (size_t)getpagesize();
	size_t stack_size = (((size_t)(ret->mem + sizeof(ret->mem) - ret->stack->base)) / page_size) * page_size;

	pthread_attr_t attr;
	void* rc;
	if(pthread_attr_init(&attr) < 0)
	    goto ERR;

	if(pthread_attr_setstack(&attr, ret->stack->base, stack_size) < 0)
	    goto ERR;

	if(pthread_create(&ret->handle, &attr, _start_main, func) < 0)
	    goto ERR;

	if(pthread_join(ret->handle, &rc) < 0)
	    goto ERR;

	free(ret);

	if(NULL == rc) return -1;
	return 0;
ERR:
	free(ret);
	return -1;
#else
	return func();
#endif
}

thread_t* thread_new(thread_main_t main, void* data, thread_type_t type)
{
	if(NULL == main) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(type != THREAD_TYPE_GENERIC)
	{
		thread_type_t mask;
		for(mask = 1; mask != 0; mask *= 2)
		    if(type == mask) break;

		if(mask == 0) ERROR_PTR_RETURN_LOG("Invalid thread type");
	}

	thread_t* ret = (thread_t*)malloc(sizeof(thread_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new thread");

	ret->main = main;
	ret->arg  = data;
	ret->hooks = NULL;
	ret->type = type;
#ifdef STACK_SIZE
	uintptr_t offset = (STACK_SIZE - ((uintptr_t)ret->mem) % STACK_SIZE) % STACK_SIZE;
	if(offset >= sizeof(_stack_t))
	    ret->stack = (_stack_t*)(ret->mem + offset - sizeof(_stack_t));
	else
	    ret->stack = (_stack_t*)(ret->mem + offset + STACK_SIZE - sizeof(_stack_t));

	size_t page_size = (size_t)getpagesize();
	size_t stack_size = (((size_t)(ret->mem + sizeof(ret->mem) - ret->stack->base)) / page_size) * page_size;

	LOG_DEBUG("Actual stack size: 0x%zx bytes", stack_size);

	ret->stack->thread = ret;

	pthread_attr_t attr;
	if(pthread_attr_init(&attr) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create attribute of the thread");

	if(pthread_attr_setstack(&attr, ret->stack->base, STACK_SIZE) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the base address of the stack");

	if(pthread_create(&ret->handle, &attr, _thread_main, ret) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot start the thread");
#else
	if(pthread_create(&ret->handle, NULL, _thread_main, ret) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot start the thread");
#endif

	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

thread_t* thread_get_current()
{
#ifdef STACK_SIZE
	return _get_current_stack()->thread;
#else
	return _thread_obj;
#endif
}

int thread_add_cleanup_hook(thread_cleanup_t func, void* data)
{
	if(NULL == func) ERROR_RETURN_LOG(int, "Invalid arguments");
	thread_t* thread = thread_get_current();

	if(NULL == thread) ERROR_RETURN_LOG(int, "Cannot add hook function to a thread not created by the thread_new function");

	_cleanup_hook_t* hook = (_cleanup_hook_t*)malloc(sizeof(_cleanup_hook_t));
	if(NULL == hook) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the cleanup hook object");

	hook->func = func;
	hook->arg  = data;
	hook->next = thread->hooks;

	thread->hooks = hook;

	LOG_DEBUG("Hook function @ <func = %p, data = %p> has been registered", func, data);
	return 0;
}

int thread_kill(thread_t* thread, int signal)
{
	if(NULL == thread) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(pthread_kill(thread->handle, signal) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot send signal to target thread");

	return 0;
}

int thread_free(thread_t* thread, void** ret)
{
	if(NULL == thread) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(pthread_join(thread->handle, ret) < 0)
	{
		free(thread);
		ERROR_RETURN_LOG(int, "Cannot join the thread");
	}

	_cleanup_hook_t* ptr, *tmp;
	for(ptr = thread->hooks; NULL != ptr;)
	{
		tmp = ptr;
		ptr = ptr->next;
		free(tmp);
	}

	free(thread);
	return 0;
}

void thread_set_name(const char* name)
{
	prctl(PR_SET_NAME, name, 0, 0, 0);
}

thread_type_t thread_get_current_type()
{
	const thread_t* thread = thread_get_current();

	return NULL != thread ? thread->type : THREAD_TYPE_GENERIC;
}

const char* thread_type_name(thread_type_t type, char* buf, size_t size)
{
	if(NULL == buf)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	string_buffer_t sbuf;

	string_buffer_open(buf, size, &sbuf);

	thread_type_t mask;
	uint32_t i, first = 1;
	for(mask = 1, i = 0; mask < THREAD_TYPE_MAX && NULL != _type_name[i]; mask <<= 1, i ++)
	    if(type & mask)
	    {
		    if(first) string_buffer_appendf(&sbuf, "[%s", _type_name[i]);
		    else      string_buffer_appendf(&sbuf, ",%s", _type_name[i]);
		    first = 0;
	    }

	string_buffer_append("]", &sbuf);

	return string_buffer_close(&sbuf);
}
