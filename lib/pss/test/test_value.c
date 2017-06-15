/**
 * Copyright (C) 2017, Feng Liu
 **/
#include <testenv.h>
#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>

void* test_mkval(void* data) 
{
	*(int*)data = 1;
	return data; 
}

int test_free(void* value) 
{
	*(int*)value = 0;
	return 0; 
}

const char* test_tostr(const void* value, char* buf, size_t bufsize)
{
	(void)value;
	(void)buf;
	(void)bufsize;
	return "<test-type>";
}

void* str_mkval(void* value)
{
	return value;
}

int str_free(void* data)
{
	free(data);
	return 0;
}

const char* str_tostr(const void* value, char* buf, size_t bufsize)
{
	(void)buf;
	(void)bufsize;
	return value;
}

int test_ref_value()
{
	int* data = (int*)calloc(1, sizeof(int));
	pss_value_t value = pss_value_ref_new(PSS_VALUE_REF_TYPE_TEST, data);

	ASSERT(value.kind == PSS_VALUE_KIND_REF, CLEANUP_NOP);
	ASSERT(pss_value_get_data(value) == data, CLEANUP_NOP);
	ASSERT(*data == 1, CLEANUP_NOP);

	ASSERT(PSS_VALUE_REF_TYPE_TEST == pss_value_ref_type(value), CLEANUP_NOP);

	ASSERT_OK(pss_value_incref(value), CLEANUP_NOP);
	ASSERT_OK(pss_value_incref(value), CLEANUP_NOP);
	ASSERT_OK(pss_value_incref(value), CLEANUP_NOP);

	pss_value_t strval = pss_value_to_str(value);
	ASSERT(strval.kind == PSS_VALUE_KIND_REF, CLEANUP_NOP);
	ASSERT(PSS_VALUE_REF_TYPE_STRING == pss_value_ref_type(strval), CLEANUP_NOP);
	ASSERT_STREQ((const char*)pss_value_get_data(strval), "<test-type>", CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(strval), CLEANUP_NOP);
	
	ASSERT_OK(pss_value_decref(value), CLEANUP_NOP);
	ASSERT(pss_value_get_data(value) == data, CLEANUP_NOP);
	ASSERT(*data == 1, CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(value), CLEANUP_NOP);
	ASSERT(pss_value_get_data(value) == data, CLEANUP_NOP);
	ASSERT(*data == 1, CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(value), CLEANUP_NOP);
	ASSERT(*data == 0, CLEANUP_NOP);

	free(data);

	return 0;
}

int test_primitive_value()
{
	pss_value_t value = {
		.kind = PSS_VALUE_KIND_NUM,
		.num  = 123
	};

	ASSERT_OK(pss_value_incref(value), CLEANUP_NOP);
	ASSERT_OK(pss_value_incref(value), CLEANUP_NOP);
	ASSERT_OK(pss_value_incref(value), CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(value), CLEANUP_NOP);
	
	pss_value_t strval = pss_value_to_str(value);
	ASSERT(strval.kind == PSS_VALUE_KIND_REF, CLEANUP_NOP);
	ASSERT(PSS_VALUE_REF_TYPE_STRING == pss_value_ref_type(strval), CLEANUP_NOP);
	ASSERT_STREQ((const char*)pss_value_get_data(strval), "123", CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(strval), CLEANUP_NOP);

	ASSERT(NULL == pss_value_get_data(value), CLEANUP_NOP);

	value.kind = PSS_VALUE_KIND_UNDEF;
	strval = pss_value_to_str(value);
	strval = strval;
	ASSERT(strval.kind == PSS_VALUE_KIND_REF, CLEANUP_NOP);
	ASSERT(PSS_VALUE_REF_TYPE_STRING == pss_value_ref_type(strval), CLEANUP_NOP);
	ASSERT_STREQ((const char*)pss_value_get_data(strval), "undefined", CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(strval), CLEANUP_NOP);
	
	ASSERT(NULL == pss_value_get_data(value), CLEANUP_NOP);

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
	ASSERT_OK(pss_value_ref_set_type_ops(PSS_VALUE_REF_TYPE_TEST, test_ops), CLEANUP_NOP);

	pss_value_ref_ops_t str_ops = {
		.mkval = str_mkval,
		.free = str_free,
		.tostr = str_tostr
	};
	ASSERT_OK(pss_value_ref_set_type_ops(PSS_VALUE_REF_TYPE_STRING, str_ops), CLEANUP_NOP);
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(test_ref_value),
	TEST_CASE(test_primitive_value)
TEST_LIST_END;
