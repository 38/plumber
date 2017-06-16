/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <testenv.h>
#include <pss.h>
int strify_test()
{
	pss_value_t dictval = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
	ASSERT(dictval.kind == PSS_VALUE_KIND_REF, CLEANUP_NOP);
	pss_dict_t* dict = (pss_dict_t*)pss_value_get_data(dictval);
	pss_value_t valbuf = {
		.kind = PSS_VALUE_KIND_NUM,
		.num  = 123
	};
	ASSERT_OK(pss_dict_set(dict, "a\nb", valbuf), CLEANUP_NOP);
	
	pss_value_t dictval2 = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
	ASSERT(dictval2.kind == PSS_VALUE_KIND_REF, CLEANUP_NOP);
	pss_dict_t* dict2 = (pss_dict_t*)pss_value_get_data(dictval2);
	pss_value_t valbuf2 = {
		.kind = PSS_VALUE_KIND_NUM,
		.num  = 456
	};
	ASSERT_OK(pss_dict_set(dict2, "x", valbuf2), CLEANUP_NOP);

	ASSERT_OK(pss_dict_set(dict, "nested", dictval2), CLEANUP_NOP);

	pss_value_t strval = pss_value_to_str(dictval);
	ASSERT(strval.kind == PSS_VALUE_KIND_REF, CLEANUP_NOP);
	ASSERT_STREQ(pss_value_get_data(strval), "{ \"a\\nb\": 123, \"nested\": { \"x\": 456 } }", CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(strval), CLEANUP_NOP);

	ASSERT_OK(pss_value_decref(dictval), CLEANUP_NOP);
	return 0;
}
int dict_test()
{
	pss_dict_t* dict;
	ASSERT_PTR(dict = pss_dict_new(), CLEANUP_NOP);

	pss_value_t value = pss_dict_get(dict, "a.b.c");
	ASSERT(PSS_VALUE_KIND_UNDEF == value.kind, goto ERR);

	pss_value_t valbuf = {
		.kind = PSS_VALUE_KIND_NUM,
		.num  = 123
	};

	ASSERT_OK(pss_dict_set(dict, "a.b.c", valbuf), goto ERR);
	
	value = pss_dict_get(dict, "a.b.c");
	ASSERT(PSS_VALUE_KIND_NUM == value.kind, goto ERR);
	ASSERT(123 == value.num, goto ERR);

	uint32_t i;
	for(i = 0; i < 500000; i ++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "var_%x", i);
		pss_value_t valbuf = {
			.kind = PSS_VALUE_KIND_NUM,
			.num = i
		};
		ASSERT_OK(pss_dict_set(dict, buf, valbuf), goto ERR);
	}

	for(i = 0; i < 500000; i ++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "var_%x", i);
		pss_value_t val = pss_dict_get(dict, buf);

		ASSERT(val.kind == PSS_VALUE_KIND_NUM, goto ERR);
		ASSERT(val.num == i, goto ERR);
	}

	ASSERT_STREQ(pss_dict_get_key(dict, 0), "a.b.c", goto ERR);

	for(i = 0; i < 500000; i ++)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "var_%x", i);

		ASSERT_STREQ(pss_dict_get_key(dict, i + 1), buf, goto ERR);
	}

	ASSERT_OK(pss_dict_free(dict), CLEANUP_NOP);

	return 0;
ERR:
	pss_dict_free(dict);
	return ERROR_CODE(int);
}

int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);
	ASSERT_OK(pss_init(), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(dict_test),
	TEST_CASE(strify_test)
TEST_LIST_END;
