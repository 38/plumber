/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <utils/static_assertion.h>
#include <execinfo.h>

#include <pthread.h>

#include <constants.h>

#ifdef __LINUX__
#define BLOCK_MAGIC_NUMBER  0x4c56f6bcu
#define _CALLER (__builtin_extract_return_addr(__builtin_return_address(0)))
extern void* __libc_malloc(size_t size);
extern void* __libc_realloc(void* ptr, size_t size);
extern void  __libc_free(void* ptr);

typedef struct _memory_block_t {
	uint32_t magic_number;
	struct _memory_block_t* prev;
	struct _memory_block_t* next;
	void* caller;
	size_t size;
	int  type;
	char data[0];
} __attribute__((packed)) memory_block_t;

STATIC_ASSERTION_SIZE(memory_block_t, data, 0);
STATIC_ASSERTION_LAST(memory_block_t, data);

static int _num_expected_memory_leakage = 0;

memory_block_t* __block_list_head = NULL;

static int __malloc_status = 0;

static int __memory_block_count = 0;

static int _initialized = 0;

static pthread_mutex_t _mutex;

static inline void _lock(void)
{
	if(!_initialized)
	{
		_initialized = 2;
		pthread_mutex_init(&_mutex, NULL);
		_initialized = 1;
	}

	pthread_mutex_lock(&_mutex);
}

static inline void _unlock(void)
{
	if(_initialized != 1) return;

	pthread_mutex_unlock(&_mutex);
}

static inline void* _block_list_append(memory_block_t* block, void* caller)
{
	_lock();
	block->type = 1;
	block->next = __block_list_head;
	block->prev = NULL;
	block->caller = caller;
	if(NULL != __block_list_head) __block_list_head->prev = block;
	__block_list_head = block;
	__memory_block_count ++;
	_unlock();
	return block->data;
}

static inline void _block_list_remove(memory_block_t* block)
{
	if(BLOCK_MAGIC_NUMBER != block->magic_number)
	{
		__malloc_status = -1;
		return;
	}
	_lock();
	if(NULL != block->prev) block->prev->next = block->next;
	if(NULL != block->next) block->next->prev = block->prev;
	__memory_block_count --;
	if(block == __block_list_head) __block_list_head = block->next;
	_unlock();
}

void* _malloc(size_t size, void* caller)
{
	size_t actual_size = size + sizeof(memory_block_t);

	memory_block_t* block = (memory_block_t*)__libc_malloc(actual_size);

	if(NULL == block) return NULL;

	block->magic_number = BLOCK_MAGIC_NUMBER;

	block->size = size;

	block->type = _initialized;

	if(_initialized == 2) return block->data;

	return _block_list_append(block, caller);
}

void* malloc(size_t size)
{
	return _malloc(size, _CALLER);
}

void free(void* ptr)
{
	if(_initialized == 2)
	{
		__libc_free(ptr);
	}

	if(NULL == ptr) return;

	memory_block_t* block = (memory_block_t*)(((char*) ptr) - offsetof(memory_block_t, data));

	if(block->type != 2) _block_list_remove(block);

	__libc_free(block);
}

void* calloc(size_t nmem, size_t size)
{
	void* ret = _malloc(nmem * size, _CALLER);

	if(NULL == ret) return NULL;

	memset(ret, 0, nmem * size);

	return ret;
}

void* realloc(void* ptr, size_t new_size)
{
	if(NULL == ptr)
	    return _malloc(new_size, _CALLER);

	memory_block_t* block = (memory_block_t*)(((char*) ptr) - offsetof(memory_block_t, data));

	size_t actual_size = new_size + sizeof(memory_block_t);

	_block_list_remove(block);

	void* caller = block->caller;

	memory_block_t* new_block = __libc_realloc(block, actual_size);

	if(NULL == new_block) return NULL;


	new_block->size = new_size;

	return _block_list_append(new_block, caller);
}

#endif

int __check_memory_allocation(void)
{
#ifdef __LINUX__
	memory_block_t* ptr;
	for(ptr = __block_list_head; ptr; _num_expected_memory_leakage--, ptr = ptr->next)
	{
		char** result = backtrace_symbols(&ptr->caller, 1);
		if(strstr(result[0], "(_dl_allocate_tls+") != NULL) _num_expected_memory_leakage ++;
		free(result);
		if(_num_expected_memory_leakage == 0) break;
	}

	return (__malloc_status != 0 || (ptr != NULL && _num_expected_memory_leakage == 0)) ? -1 : 0;
#else
	return 0;
#endif
}

void __print_memory_leakage(void)
{
#ifdef __LINUX__
	memory_block_t* ptr;
	for(ptr = __block_list_head; ptr; ptr = ptr->next)
	{
		char** result = backtrace_symbols(&ptr->caller, 1);
		fprintf(stderr, "Possible %zu bytes memory leak at %p,allocator = %s\n", ptr->size, ptr->data, result[0]);
		free(result);
	}

	pthread_mutex_destroy(&_mutex);
#else
	return;
#endif
}
void expected_memory_leakage(void)
{
#ifdef __LINUX__
	_num_expected_memory_leakage ++;
#endif
}

