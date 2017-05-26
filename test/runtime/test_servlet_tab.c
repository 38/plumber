/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>

runtime_stab_entry_t id;

int test_load_servlet()
{

	const char* argv[] = {"serv_loader_test"};

	ASSERT_RETOK(runtime_stab_entry_t, id = runtime_stab_load(1, argv), CLEANUP_NOP);
	expected_memory_leakage();

	LOG_DEBUG("Servlet reference id = %u", id);

	return 0;
}

int test_servlet_not_found()
{
	const char* argv[] = {"__servlet_not_exist__"};

	ASSERT(runtime_stab_load(1, argv) == ERROR_CODE(runtime_stab_entry_t), CLEANUP_NOP);

	return 0;
}

int test_servlet_invalid_args()
{
	ASSERT(runtime_stab_load(0, NULL) == ERROR_CODE(runtime_stab_entry_t), CLEANUP_NOP);

	return 0;
}

int test_servlet_num_pipes()
{
	ASSERT(runtime_stab_num_pipes(id) == 5, CLEANUP_NOP);

	ASSERT(runtime_stab_num_pipes(ERROR_CODE(runtime_stab_entry_t)) == ERROR_CODE(size_t), CLEANUP_NOP);

	return 0;
}

int test_servlet_find_pipe_by_name()
{
	ASSERT(runtime_stab_get_pipe(id, "test_pipe_0") == 2, CLEANUP_NOP);
	ASSERT(runtime_stab_get_pipe(id, "test_pipe_1") == 3, CLEANUP_NOP);
	ASSERT(runtime_stab_get_pipe(id, "test_pipe_2") == 4, CLEANUP_NOP);
	ASSERT(runtime_stab_get_pipe(id, "test_pipe_3") == ERROR_CODE(runtime_api_pipe_id_t), CLEANUP_NOP);

	return 0;
}

int test_servlet_pipe_count()
{
	ASSERT(runtime_stab_get_num_input_pipe(id) == 2, CLEANUP_NOP);
	ASSERT(runtime_stab_get_num_output_pipe(id) == 1, CLEANUP_NOP);

	return 0;
}
int setup()
{
	if(runtime_servlet_append_search_path(TESTDIR) < 0) return -1;
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_load_servlet),
    TEST_CASE(test_servlet_not_found),
    TEST_CASE(test_servlet_num_pipes),
    TEST_CASE(test_servlet_find_pipe_by_name),
    TEST_CASE(test_servlet_num_pipes),
    TEST_CASE(test_servlet_invalid_args)
TEST_LIST_END;
