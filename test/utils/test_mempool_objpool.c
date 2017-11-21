/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <utils/mempool/objpool.h>

mempool_objpool_t* pool;

int pool_creation(void)
{
	ASSERT_PTR(pool = mempool_objpool_new(11), CLEANUP_NOP);
	ASSERT((mempool_objpool_get_obj_size(pool) & (sizeof(uintptr_t) - 1)) == 0, CLEANUP_NOP);
	ASSERT((mempool_objpool_get_obj_size(pool) > 11), CLEANUP_NOP);
	return 0;
}
int compare(const void* a, const void* b)
{
	return (int)(*(uintptr_t*)a - *(uintptr_t*)b);
}
static inline uintptr_t _p2u(void* mem)
{
	return *(uintptr_t*)mem;
}
int pool_allocation(void)
{
	void* mem;
	ASSERT_PTR(mem = mempool_objpool_alloc(pool), CLEANUP_NOP);
	uintptr_t addr = _p2u(&mem);
	ASSERT((addr & (sizeof(uintptr_t) - 1)) == 0, CLEANUP_NOP);
	memset(mem, -1, 11);
	ASSERT_OK(mempool_objpool_dealloc(pool, mem), CLEANUP_NOP);
	ASSERT(mempool_objpool_get_page_count(pool) == 1, CLEANUP_NOP);

	void *mem2;
	ASSERT_PTR(mem2 = mempool_objpool_alloc(pool), CLEANUP_NOP);
	ASSERT(mem == mem2, CLEANUP_NOP);
	ASSERT(mempool_objpool_get_page_count(pool) == 1, CLEANUP_NOP);

	uint32_t i;
	void* last = NULL;
	for(i = 0; i < 4096; i ++)
	{
		void* p;
		ASSERT_PTR(p = mempool_objpool_alloc(pool), CLEANUP_NOP);
		ASSERT_OK(mempool_objpool_dealloc(pool, p), CLEANUP_NOP);
		ASSERT(p != mem, CLEANUP_NOP);
		if(NULL != last)
		{
			ASSERT(last == p, CLEANUP_NOP);
		}
		last = p;
	}


	void *ptr[4096];
	for(i = 0; i < 4096; i ++)
	    ASSERT_PTR(ptr[i] = mempool_objpool_alloc(pool), CLEANUP_NOP);

	qsort(ptr, 4096, sizeof(void*), compare);

	for(i = 1; i < 4096; i ++)
	    ASSERT(ptr[i-1] != ptr[i], CLEANUP_NOP);

	for(i = 0; i < 4096; i ++)
	    ASSERT_OK(mempool_objpool_dealloc(pool, ptr[i]), CLEANUP_NOP);

	uint32_t pc = mempool_objpool_get_page_count(pool);

	qsort(ptr, 4096, sizeof(void*), compare);

	for(i = 1; i < 4096; i ++)
	    ASSERT(ptr[i-1] != ptr[i], CLEANUP_NOP);

	for(i = 0; i < 4096; i ++)
	    ASSERT_PTR(ptr[i] = mempool_objpool_alloc(pool), CLEANUP_NOP);

	ASSERT(pc == mempool_objpool_get_page_count(pool), CLEANUP_NOP);

	for(i = 0; i < 4096; i ++)
	    ASSERT_OK(mempool_objpool_dealloc(pool, ptr[i]), CLEANUP_NOP);

	ASSERT(pc == mempool_objpool_get_page_count(pool), CLEANUP_NOP);
	return 0;
}

int disabled_pool(void)
{
	uint32_t i;

	void *ptr[4096];
	for(i = 0; i < 4096; i ++)
	    ASSERT_PTR(ptr[i] = mempool_objpool_alloc(pool), CLEANUP_NOP);

	for(i = 0; i < 4096; i ++)
	    ASSERT_OK(mempool_objpool_dealloc(pool, ptr[i]), CLEANUP_NOP);

	ASSERT_OK(mempool_objpool_disabled(1), CLEANUP_NOP);

	void* mem;
	ASSERT_PTR(mem = mempool_objpool_alloc(pool), CLEANUP_NOP);
	for(i = 0; i < 4096; i ++)
	    ASSERT(mem != ptr[i], CLEANUP_NOP);

	ASSERT_OK(mempool_objpool_dealloc(pool, mem), CLEANUP_NOP);

	ASSERT_OK(mempool_objpool_disabled(0), CLEANUP_NOP);

	ASSERT_PTR(mem = mempool_objpool_alloc(pool), CLEANUP_NOP);
	for(i = 0; i < 4096; i ++)
	    if(mem == ptr[i]) break;
	ASSERT(i < 4096, CLEANUP_NOP);


	return 0;
}
int setup(void)
{
	return mempool_objpool_disabled(0);
}

int teardown(void)
{
	return mempool_objpool_free(pool);
}

TEST_LIST_BEGIN
    TEST_CASE(pool_creation),
    TEST_CASE(pool_allocation),
    TEST_CASE(disabled_pool)
TEST_LIST_END;
