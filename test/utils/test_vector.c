/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdlib.h>
#include <testenv.h>
#include <utils/vector.h>

static vector_t* vec = NULL;

int test_vector_insersion(void)
{
	uint32_t i;
	for(i = 0; i < 5; i ++)
		ASSERT_PTR(vec = vector_append(vec, &i) , CLEANUP_NOP);

	for(i = 0; i < 5; i ++)
		ASSERT(*VECTOR_GET_CONST(uint32_t, vec, i) == i, CLEANUP_NOP);

	return 0;
}

int test_vector_resize(void)
{
	uint32_t i;
	for(i = 5; i < 128; i ++)
		ASSERT_PTR(vec = vector_append(vec, &i), CLEANUP_NOP);

	for(i = 0; i < 128; i ++)
		ASSERT(*VECTOR_GET_CONST(uint32_t, vec, i) == i, CLEANUP_NOP);

	return 0;
}

int test_vector_traverse(void)
{
	uint32_t i;

	for(i = 0; i < vector_length(vec); i ++)
	{
		ASSERT(*(VECTOR_GET_CONST(uint32_t, vec, i)) == i, CLEANUP_NOP);
		*VECTOR_GET(uint32_t, vec, i) = i * 2;
	}

	for(i = 0; i < 128; i ++)
		ASSERT(*VECTOR_GET_CONST(uint32_t, vec, i) == i * 2, CLEANUP_NOP);
	return 0;
}

int test_vector_clean(void)
{
	vector_clear(vec);
	ASSERT(0 == vector_length(vec), CLEANUP_NOP);

	return 0;
}

int setup(void)
{
	ASSERT(vec = vector_new(sizeof(uint32_t), 8), CLEANUP_NOP);
	return 0;
}

int teardown(void)
{
	ASSERT(NULL != vec && vector_free(vec) >= 0, if(NULL != vec) free(vec));
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_vector_insersion),
    TEST_CASE(test_vector_resize),
    TEST_CASE(test_vector_traverse),
    TEST_CASE(test_vector_clean)
TEST_LIST_END;
