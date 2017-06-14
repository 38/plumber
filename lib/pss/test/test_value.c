/**
 * Copyright (C) 2017, Feng Liu
 **/
#include <testenv.h>
#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>

void* test_mkval(void* data) { return data; }

int test_free(void* value) { free(value); return 0; }

const char* test_tostr(const void* value, char* buf, size_t bufsize)
{
	(void)value;
	(void)buf;
	(void)bufsize;
	return "This is a test string";
}

const char* str_tostr(const void* value, char* buf, size_t bufsize)
{
	(void)buf;
	(void)bufsize;
	return value;
}

int test_value()
{
	return 0;
}

int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);
	pss_value_ref_ops_t test_ops = {
		.mkval = test_mkval,
		.free = test_free,
		.tostr = test_tostr
	};
	ASSERT_OK(pss_value_set_type_ops(PSS_VALUE_REF_TYPE_TEST, test_ops), CLEANUP_NOP);
	pss_value_ref_ops_t str_ops = {
		.mkval = test_mkval,
		.free = test_free,
		.tostr = str_tostr
	};
	ASSERT_OK(pss_value_set_type_ops(PSS_VALUE_REF_TYPE_STRING, str_ops), CLEANUP_NOP);
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(test_value)
TEST_LIST_END;
