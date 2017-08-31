/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>

runtime_servlet_binary_t* binary;
runtime_servlet_t* servlet;
runtime_task_t* task;
int task_started = 0;
static void trap(int id)
{
	if(id == 0)
	{
		task = runtime_task_current();
	}
	else if(id == 1)
	{
		task = runtime_task_current();
		if(task->flags == (RUNTIME_TASK_FLAG_ACTION_EXEC | 3 | RUNTIME_TASK_FLAG_ACTION_INVOKED))
		{
			LOG_DEBUG("Successfully verified the task");
			task_started = 1;
		}
		else
		{
			LOG_ERROR("Wrong task flags");
		}
	}
	else if(id == 2)
	{
		task = runtime_task_current();
		LOG_DEBUG("Unload has been called");

	}
}

int test_get_current_task()
{
	const char* argv[] = {"task_test"};

	ASSERT_OK(runtime_servlet_set_trap(trap), CLEANUP_NOP);

	const char* path = runtime_servlet_find_binary ("serv_task_test");

	LOG_DEBUG("Binary path: %s", path);

	ASSERT_PTR(path, CLEANUP_NOP);

	ASSERT_PTR(binary = runtime_servlet_binary_load(path, "task_test"), CLEANUP_NOP);
	expected_memory_leakage();

	ASSERT_PTR(servlet = runtime_servlet_new(binary, 1, argv), CLEANUP_NOP);

	ASSERT_PTR(task, CLEANUP_NOP);

	return 0;
}

int test_exec_task()
{
	runtime_task_t* task = runtime_task_new(servlet, (RUNTIME_TASK_FLAG_ACTION_EXEC | 3));

	ASSERT_PTR(task, CLEANUP_NOP);

	ASSERT_OK(runtime_task_start(task, NULL), runtime_task_free(task));

	ASSERT_OK(runtime_task_free(task), CLEANUP_NOP);

	ASSERT(task_started == 1, CLEANUP_NOP);
	return 0;
}


int setup()
{
	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);

	return 0;
}

int teardown()
{
	ASSERT_OK(runtime_servlet_free(servlet), CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_binary_unload(binary), CLEANUP_NOP);
	ASSERT_PTR(task, CLEANUP_NOP);
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_get_current_task),
    TEST_CASE(test_exec_task)
TEST_LIST_END;
