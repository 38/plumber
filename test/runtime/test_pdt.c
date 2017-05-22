/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <stdio.h>

runtime_pdt_t* pdt = NULL;

int test_pdt_creation()
{
	ASSERT_PTR(pdt = runtime_pdt_new(), CLEANUP_NOP);

	return 0;
}

int test_pdt_insertion()
{
	int i;
	for(i = 0; i < 100; i ++)
	{
		static char buffer[20];
		snprintf(buffer, sizeof(buffer), "test_pipe_#%d", i);
		ASSERT_RETOK(runtime_api_pipe_id_t, runtime_pdt_insert(pdt, buffer, (runtime_api_pipe_flags_t)(i * 2), "$T"), CLEANUP_NOP);
	}
	return 0;
}

int test_pdt_find()
{
	int i;
	for(i = 0; i < 100; i ++)
	{
		static char buffer[20];
		snprintf(buffer, sizeof(buffer), "test_pipe_#%d", i);
		ASSERT(runtime_pdt_get_pd_by_name(pdt, buffer) == i, CLEANUP_NOP);
	}

	ASSERT(runtime_pdt_get_pd_by_name(pdt, "__pipe_name_not_exist__") == ERROR_CODE(runtime_api_pipe_id_t), CLEANUP_NOP);

	return 0;
}

int test_pdt_flags()
{
	uint32_t i;
	for(i = 0; i < 100; i ++)
	{
		ASSERT(runtime_pdt_get_flags_by_pd(pdt, (runtime_api_pipe_id_t)i) == (runtime_api_pipe_id_t)(i * 2), CLEANUP_NOP);
	}
	return 0;
}

int test_pdt_size1()
{
	ASSERT(runtime_pdt_get_size(pdt) == 0, CLEANUP_NOP);

	return 0;
}

int test_pdt_size2()
{
	ASSERT(runtime_pdt_get_size(pdt) == 100, CLEANUP_NOP);

	return 0;
}

int test_pdt_input_output_count()
{
#define CLEANUP if(pdt != NULL) runtime_pdt_free(pdt);
	runtime_pdt_t* pdt = runtime_pdt_new();

	ASSERT_PTR(pdt, CLEANUP);

	ASSERT(runtime_pdt_input_count(pdt) == 0, CLEANUP);
	ASSERT(runtime_pdt_output_count(pdt) == 0, CLEANUP);

	int i;
	for(i = 0; i < 100; i ++)
	{
		static char buffer[20];
		snprintf(buffer, sizeof(buffer), "test_pipe_#%d", i);
		ASSERT_RETOK(runtime_api_pipe_id_t, runtime_pdt_insert(pdt, buffer, RUNTIME_API_PIPE_INPUT, "$T"), CLEANUP);
		ASSERT(runtime_pdt_input_count(pdt) == i + 1, CLEANUP);
		ASSERT(runtime_pdt_output_count(pdt) == 0, CLEANUP);
	}

	for(i = 0; i < 100; i ++)
	{
		static char buffer[20];
		snprintf(buffer, sizeof(buffer), "test_pipe_#%d", i + 100);
		ASSERT_RETOK(runtime_api_pipe_id_t, runtime_pdt_insert(pdt, buffer, RUNTIME_API_PIPE_OUTPUT, "$S"), CLEANUP);
		ASSERT(runtime_pdt_input_count(pdt) == 100, CLEANUP);
		ASSERT(runtime_pdt_output_count(pdt) == i + 1, CLEANUP);
	}

	ASSERT_OK(runtime_pdt_free(pdt), LOG_ERROR("Cannot dispose testing PDT"));

	return 0;
}
DEFAULT_SETUP;

int teardown()
{
	ASSERT_OK(runtime_pdt_free(pdt), CLEANUP_NOP);

	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_pdt_creation),
    TEST_CASE(test_pdt_size1),
    TEST_CASE(test_pdt_insertion),
    TEST_CASE(test_pdt_find),
    TEST_CASE(test_pdt_flags),
    TEST_CASE(test_pdt_size2),
    TEST_CASE(test_pdt_input_output_count)
TEST_LIST_END;
