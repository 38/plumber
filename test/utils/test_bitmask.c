/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <utils/bitmask.h>
#include <time.h>
#include <stdlib.h>
bitmask_t* bitmask;
int test_alloc(void)
{
	size_t i;
	for(i = 0; i < 1234567; i ++)
	    ASSERT(i == bitmask_alloc(bitmask), CLEANUP_NOP);

	return 0;
}

int test_full_alloc(void)
{
	ASSERT((size_t)-1 == bitmask_alloc(bitmask), CLEANUP_NOP);

	return 0;
}

int test_dealloc(void)
{
	size_t i;
	for(i = 0; i < 1234567; i += 3)
	    ASSERT_OK(bitmask_dealloc(bitmask, i), CLEANUP_NOP);
	return 0;
}

int test_reuse(void)
{
	size_t i;
	for(i = 0; i < 1234567; i += 3)
	    ASSERT(bitmask_alloc(bitmask) == i, CLEANUP_NOP);

	ASSERT(bitmask_alloc(bitmask) == (size_t)-1, CLEANUP_NOP);
	return 0;
}
static inline int _test_ops(int k)
{
	static int flg[1234567] = {};
	size_t i;
	static size_t used = 0;
	for(i = 0; i < 10000000; i ++)
	{
		int op = rand() & 3;
		if(op < k)
		{
			if(used < 1234567)
			{
				size_t rc;
				ASSERT_RETOK(size_t, rc = bitmask_alloc(bitmask), CLEANUP_NOP);
				ASSERT(flg[rc] == 0, CLEANUP_NOP);
				flg[rc] = 1;
				used ++;
			}
			else
			{
				ASSERT(bitmask_alloc(bitmask) == ERROR_CODE(size_t), CLEANUP_NOP);
			}
		}
		else
		{
			size_t v = (size_t)rand() % 1234567;
			ASSERT_OK(bitmask_dealloc(bitmask, v), CLEANUP_NOP);
			if(flg[v] == 1) used --;
			flg[v] = 0;
		}
	}
	return 0;
}
int test_random_ops(void)
{
	srand((unsigned)time(NULL));
	ASSERT_OK(bitmask_clear(bitmask), CLEANUP_NOP);
	ASSERT_OK(_test_ops(3), CLEANUP_NOP);
	ASSERT_OK(_test_ops(2), CLEANUP_NOP);
	ASSERT_OK(_test_ops(1), CLEANUP_NOP);
	return 0;
}

int setup(void)
{
	bitmask = bitmask_new(1234567);
	ASSERT_PTR(bitmask, CLEANUP_NOP);

	return 0;
}

int teardown(void)
{
	bitmask_free(bitmask);

	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_alloc),
    TEST_CASE(test_full_alloc),
    TEST_CASE(test_dealloc),
    TEST_CASE(test_reuse),
    TEST_CASE(test_random_ops)
TEST_LIST_END;
