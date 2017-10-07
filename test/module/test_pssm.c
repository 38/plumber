/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <constants.h>
#include <testenv.h>
#include <pthread.h>
#include <module/builtins.h>

#include <utils/thread.h>

runtime_stab_entry_t mem_pool_sid;
runtime_stab_entry_t thread_local_test_sid;
int thread_local_test_ok;
int32_t* thread_local_test_buffer;

int test_mem_pool()
{
	runtime_task_t* task;
	ASSERT_PTR(task = runtime_stab_create_exec_task(mem_pool_sid, RUNTIME_TASK_FLAG_ACTION_EXEC), CLEANUP_NOP);

	ASSERT_OK(runtime_task_start(task), CLEANUP_NOP);

	ASSERT_OK(runtime_task_free(task), CLEANUP_NOP);

	return 0;
}

int test_method()
{
	int i;
	for(i = 0; i < 100; i ++)
	{
		runtime_task_t* task;
		ASSERT_PTR(task = runtime_stab_create_exec_task(thread_local_test_sid, RUNTIME_TASK_FLAG_ACTION_EXEC), CLEANUP_NOP);

		ASSERT_OK(runtime_task_start(task), CLEANUP_NOP);

		ASSERT_OK(runtime_task_free(task), CLEANUP_NOP);
	}
	return 0;
}
void* test_thread(void* data)
{
	(void) data;
	if(test_method() == ERROR_CODE(int))
	    thread_local_test_ok = 0;
	return NULL;
}

int test_thread_local()
{
#ifdef __LINUX__
	thread_t* thread[32];
	int i;
	for(i = 0; i < 32; i ++)
	    ASSERT_PTR(thread[i] = thread_new(test_thread, NULL, THREAD_TYPE_GENERIC), CLEANUP_NOP);
	for(i = 0; i < 32; i ++)
	    ASSERT_OK(thread_free(thread[i], NULL), CLEANUP_NOP);

	ASSERT_OK(thread_local_test_ok, CLEANUP_NOP);

	int count = 0;
	for(i = 0; i < 64; i ++)
	{
		LOG_DEBUG("thread_local_test_buffer[%d] = %d", i, thread_local_test_buffer[i]);
		if(thread_local_test_buffer[i] == 100) count ++;
	}

	ASSERT(count == 32, CLEANUP_NOP);
#endif
	return 0;
}

int setup()
{

#if __i386__
	int i;
	for(i = 0; i < 18; i ++)
	    expected_memory_leakage();
#endif
	ASSERT_OK(mempool_objpool_disabled(0), CLEANUP_NOP);

	ASSERT_OK(itc_modtab_insmod(&module_pssm_module_def, 0, NULL), CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);

	thread_local_test_ok = 1;

	static int32_t __buffer[64];
	thread_local_test_buffer = __buffer;

	{
		char const* argv[] = {"serv_mempool_test"};
		ASSERT_RETOK(runtime_stab_entry_t, mem_pool_sid = runtime_stab_load(1, argv), CLEANUP_NOP);
		expected_memory_leakage();
	}

	{
		char const* argv[] = {"serv_thread_local_test"};
		ASSERT_RETOK(runtime_stab_entry_t, thread_local_test_sid = runtime_stab_load(1, argv), CLEANUP_NOP);
		expected_memory_leakage();
	}

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_mem_pool),
    TEST_CASE(test_thread_local)
TEST_LIST_END;
