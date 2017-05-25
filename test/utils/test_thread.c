/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <utils/thread.h>
#define N 128
uint32_t data[N * 2];
uint32_t data2[N * 2];

thread_pset_t* pset1, *pset2;

static void* alloc1(uint32_t i, const void* d)
{
	(void)d;
	LOG_DEBUG("Allocating memory for %u", i);
	return data + i;
}
static int dealloc1(void* mem, const void * d)
{
	(void)d;
	(void)mem;
	return 0;
}

static void* alloc2(uint32_t i, const void* d)
{
	(void)d;
	LOG_DEBUG("Allocating memory for %u", i);
	return data2 + i;
}
static int dealloc2(void* mem, const void* d)
{
	(void)d;
	(void)mem;
	return 0;
}


int create()
{
	ASSERT_PTR(pset1 = thread_pset_new(1, alloc1, dealloc1, NULL), CLEANUP_NOP);
	ASSERT_PTR(pset2 = thread_pset_new(1, alloc2, dealloc2, NULL), CLEANUP_NOP);

	return 0;
}

static inline void* thread_main(void* ptr)
{
	(void)ptr;

	uint32_t i;
	uint32_t* p, *q;
	for(i = 0; i < 10; i ++)
	{
		p = (uint32_t*)thread_pset_acquire(pset1);

		if(NULL == p) return NULL;

		(*p)++;

		q = (uint32_t*)thread_pset_acquire(pset2);
		if(NULL == q) return NULL;
		(*q) += 2;
	}

	return p;
}
int run()
{
	thread_t* t[N];
	uint32_t i;
	for(i = 0; i < N; i ++)
	    ASSERT_PTR(t[i] = thread_new(thread_main, NULL, THREAD_TYPE_GENERIC), CLEANUP_NOP);

	for(i = 0; i < N; i ++)
	{
		void* ret;
		ASSERT_OK(thread_free(t[i], &ret), CLEANUP_NOP);
		ASSERT_PTR(ret, CLEANUP_NOP);
	}
	return 0;
}
int dispose()
{
	uint32_t i, j = 0;
	for(i = 0; i < 2 * N; i ++)
		if(data[i] == 10) j ++;
	ASSERT(j == N, CLEANUP_NOP);

	j = 0;
	for(i = 0; i < 2 * N; i ++)
		if(data[i] == 10) j ++;
	ASSERT(j == N, CLEANUP_NOP);

	ASSERT_OK(thread_pset_free(pset1), CLEANUP_NOP);
	ASSERT_OK(thread_pset_free(pset2), CLEANUP_NOP);

	return 0;
}
#define NT 16
int flag[NT] = {};
int cleanup(void* ta, void* ca)
{
	int n = *(int*)ta;
	int s = (int)(((int*)ca) - flag);

	ASSERT(flag[n] / 8 > s, CLEANUP_NOP);

	if((flag[n]&0x3) == 0x3) flag[n] |= 0x4;

	flag[n] = flag[n] & 0x7;
	flag[n] += (s) * 8;
	return 0;
}

void* test_thread_main(void* arg)
{
	int n = *(int*)arg;
	flag[n] |= 0x1;

	uint32_t i;
	for(i = 0; i < NT; i ++)
	{
		if(thread_add_cleanup_hook(cleanup, flag + i) == ERROR_CODE(int))
		    return NULL;
	}
	flag[n] |= 0x2;

	flag[n] |= NT * 8;

	return flag + n;
}

int thread_obj()
{
	thread_t* threads[NT];
	int n[NT];
	uint32_t i;
	for(i = 0; i < NT; i ++)
	{
		n[i] = (int)i;
		ASSERT_PTR(threads[i] = thread_new(test_thread_main, n + i, THREAD_TYPE_GENERIC), CLEANUP_NOP);
	}

	for(i = 0; i < NT; i ++)
	{
		void* ret;
		ASSERT_OK(thread_free(threads[i], &ret), CLEANUP_NOP);

		ASSERT(ret == flag + i, CLEANUP_NOP);
	}

	for(i = 0; i < NT; i ++)
	    ASSERT(flag[i] == 0x7, CLEANUP_NOP);

	return 0;
}

int setup()
{
#if __i386__
	int i;
	for(i = 0; i < 19; i ++)
	    expected_memory_leakage();
#endif
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(create),
    TEST_CASE(run),
    TEST_CASE(dispose),
    TEST_CASE(thread_obj)
TEST_LIST_END;
