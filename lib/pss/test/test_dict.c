/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <testenv.h>
#include <pss.h>
int dict_test()
{
	pss_dict_t* dict;
	ASSERT_PTR(dict = pss_dict_new(), CLEANUP_NOP);

	pss_value_const_t value = pss_dict_get(dict, "a.b.c");
	ASSERT(PSS_VALUE_KIND_UNDEF == value.kind, CLEANUP_NOP);

	pss_value_t valbuf = {
		.kind = PSS_VALUE_KIND_NUM,
		.num  = 123
	};

	ASSERT_OK(pss_dict_set(dict, "a.b.c", valbuf), CLEANUP_NOP);
	
	value = pss_dict_get(dict, "a.b.c");
	ASSERT(PSS_VALUE_KIND_NUM == value.kind, CLEANUP_NOP);
	ASSERT(123 == value.num, CLEANUP_NOP);

	uint32_t i;
	for(i = 0; i < 1000; i ++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "var_%x", i);
		pss_value_t valbuf = {
			.kind = PSS_VALUE_KIND_NUM,
			.num = i
		};
		ASSERT_OK(pss_dict_set(dict, buf, valbuf), CLEANUP_NOP);
	}

	for(i = 0; i < 1000; i ++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "var_%x", i);
		pss_value_const_t val = pss_dict_get(dict, buf);

		ASSERT(val.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
		ASSERT(val.num == i, CLEANUP_NOP);
	}

	ASSERT_OK(pss_dict_free(dict), CLEANUP_NOP);

	return 0;
}

int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(dict_test)
TEST_LIST_END;
