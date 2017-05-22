/**
 * Copyright (C) 2017, Kejun Li
 **/

#include <testenv.h>
#include <utils/mempool/oneway.h>

mempool_oneway_t* table;

int test_allocate()
{
	table = mempool_oneway_new(1024);
	ASSERT_PTR(table, CLEANUP_NOP);
	char* strs = (char *) mempool_oneway_alloc(table, 10);
	ASSERT_PTR(strs, CLEANUP_NOP);
	strcpy(strs, "test");
	ASSERT(strcmp(strs, "test") == 0, CLEANUP_NOP);
	ASSERT_OK(mempool_oneway_free(table), CLEANUP_NOP);
	return 0;
}

// default size 1024, allocate 100 for both pointers, their address should not be overlapping.
int test_memory_in_same_block()
{
	table = mempool_oneway_new(1024);

	ASSERT_PTR(table, CLEANUP_NOP);
	char* space1 = mempool_oneway_alloc(table, 100);
	char* space2 = mempool_oneway_alloc(table, 100);

	ASSERT_PTR(space1, CLEANUP_NOP);
	ASSERT_PTR(space2, CLEANUP_NOP);

	ASSERT((space2 > space1 ? space2 - space1 : space1 - space2) >= 100, CLEANUP_NOP);
	ASSERT_OK(mempool_oneway_free(table), CLEANUP_NOP);
	return 0;
}

int test_memory_in_different_block()
{
	table = mempool_oneway_new(1024);
	ASSERT_PTR(table, CLEANUP_NOP);

	void* space1 = mempool_oneway_alloc(table, 100);
	void* space2 = mempool_oneway_alloc(table, 1024);

	ASSERT_PTR(space1, CLEANUP_NOP);
	ASSERT_PTR(space2, CLEANUP_NOP);

	ASSERT(space2 != space1+100, CLEANUP_NOP);
	ASSERT_OK(mempool_oneway_free(table), CLEANUP_NOP);
	return 0;
}

int setup()
{
	return 0;
}

int teardown()
{
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_allocate),
    TEST_CASE(test_memory_in_same_block),
    TEST_CASE(test_memory_in_different_block)
TEST_LIST_END;
