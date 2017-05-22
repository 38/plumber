/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
int test_add_search_path()
{
	ASSERT(runtime_servlet_num_search_path() == 0, CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_append_search_path("test0"), CLEANUP_NOP);
	ASSERT(runtime_servlet_num_search_path() == 1, CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_append_search_path("test1"), CLEANUP_NOP);
	ASSERT(runtime_servlet_num_search_path() == 2, CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_append_search_path("test2"), CLEANUP_NOP);
	ASSERT(runtime_servlet_num_search_path() == 3, CLEANUP_NOP);
	const char* const* path_list = runtime_servlet_search_paths();
	ASSERT_PTR(path_list, CLEANUP_NOP);
	ASSERT_STREQ(path_list[0], "test0", CLEANUP_NOP);
	ASSERT_STREQ(path_list[1], "test1", CLEANUP_NOP);
	ASSERT_STREQ(path_list[2], "test2", CLEANUP_NOP);

	ASSERT_OK(runtime_servlet_clear_search_path(), CLEANUP_NOP);

	ASSERT(runtime_servlet_clear_search_path() == 0, CLEANUP_NOP);

	return 0;
}

static runtime_servlet_binary_t* _test_binary;
static runtime_servlet_t* _test_servlet;

int test_load_servlet()
{
	const char* argv[] = {"serv_loader_test"};
	ASSERT_OK(runtime_servlet_clear_search_path(), CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);
	const char* path = runtime_servlet_find_binary ("serv_loader_test");
	LOG_DEBUG("Binary path: %s", path);
	ASSERT_PTR(path, CLEANUP_NOP);
	ASSERT_PTR(_test_binary = runtime_servlet_binary_load(path, "serv_loader_test"), CLEANUP_NOP);
	ASSERT_PTR(_test_servlet = runtime_servlet_new(_test_binary, 1, argv), CLEANUP_NOP);
	expected_memory_leakage(); /* Because a GLIBC bug, we are expecting a memory leak here */
	return 0;
}

int test_unload_servlet()
{
	ASSERT_PTR(_test_servlet, CLEANUP_NOP);

	ASSERT_OK(runtime_servlet_free(_test_servlet), CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_binary_unload(_test_binary), CLEANUP_NOP);

	return 0;
}

int test_servlet_not_found()
{
	ASSERT(runtime_servlet_num_search_path() == 1, CLEANUP_NOP);
	ASSERT_STREQ(runtime_servlet_search_paths()[0], TESTDIR, CLEANUP_NOP);

	const char* path = runtime_servlet_find_binary("servlet_not_exist");

	ASSERT(NULL == path, CLEANUP_NOP);

	return 0;
}

int setup()
{
	//IGNORE_MEMORY_LEAK();
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_add_search_path),
    TEST_CASE(test_load_servlet),
    TEST_CASE(test_unload_servlet),
    TEST_CASE(test_servlet_not_found)
TEST_LIST_END;
