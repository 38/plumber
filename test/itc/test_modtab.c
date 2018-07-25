/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <stdio.h>
#include <itc/module_types.h>
#include <module/test/module.h>
#define N 250

int test_insmod(void)
{
	int i;
	static char name[N][32];
	for(i = 0; i < N; i ++)
	{
		char const* args[1];
		args[0] = name[i];
		snprintf(name[i], 32, "__test_%d", i);
		ASSERT_OK(itc_modtab_insmod(&module_test_module_def, 1, args), CLEANUP_NOP);
	}

	return 0;
}

int test_findmod(void)
{
	int i;
	static char name[32];
	static int flag[256] = {};
	int count = 0;
	for(i = 0; i < N; i ++)
	{
		snprintf(name, 32, "pipe.test.__test_%d", i);
		const itc_modtab_instance_t* result = itc_modtab_get_from_path(name);
		ASSERT_PTR(result, CLEANUP_NOP);
		if(flag[result->module_id] == 0) flag[result->module_id] = 1, count ++;
	}
	LOG_DEBUG("Count = %d", count);
	ASSERT(count == N, CLEANUP_NOP);

	ASSERT(NULL == itc_modtab_get_from_path("pipe.test.__test_300"), CLEANUP_NOP);
	return 0;
}

int test_opendir(void)
{
	itc_modtab_dir_iter_t iter;
	const itc_modtab_instance_t* entry;

	ASSERT_OK(itc_modtab_open_dir("pipe.test.__test_", &iter), CLEANUP_NOP);

	int count = 0;

	for(;(entry = itc_modtab_dir_iter_next(&iter)) != NULL; count ++)
		LOG_DEBUG("Module 0x%x: @%p with context @%p", entry->module_id, entry->module, entry->context);

	ASSERT(N == count, CLEANUP_NOP);

	count = 0;

	ASSERT_OK(itc_modtab_open_dir("pipe.test.__test_2", &iter), CLEANUP_NOP);

	for(;(entry = itc_modtab_dir_iter_next(&iter)) != NULL; count ++)
		LOG_DEBUG("Module 0x%x: @%p with context @%p", entry->module_id, entry->module, entry->context);

	ASSERT(50 /* 2xx */ + 10 /* 2x */ + 1  == count, CLEANUP_NOP);

	ASSERT_OK(itc_modtab_open_dir("", &iter), CLEANUP_NOP);

	count = 0;

	for(;(entry = itc_modtab_dir_iter_next(&iter)) != NULL; count ++)
		LOG_DEBUG("Module 0x%x: @%p with context @%p", entry->module_id, entry->module, entry->context);

	ASSERT(N <= count, CLEANUP_NOP);

	return 0;
}

DEFAULT_SETUP;

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_insmod),
    TEST_CASE(test_findmod),
    TEST_CASE(test_opendir)
TEST_LIST_END;
