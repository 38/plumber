/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>
int frame_test()
{
	pss_frame_t* frame = pss_frame_new(NULL);
	ASSERT_PTR(frame, goto ERR);

	pss_value_t value = {
		.kind = PSS_VALUE_KIND_NUM,
		.num = 456
	};
	pss_value_const_t result;

	ASSERT_OK(pss_frame_reg_set(frame, 123, value), goto ERR);

	result = pss_frame_reg_get(frame, 123);
	ASSERT(result.kind == PSS_VALUE_KIND_NUM, goto ERR);
	ASSERT(result.num == 456, goto ERR);

	result = pss_frame_reg_get(frame, 12345);
	ASSERT(result.kind == PSS_VALUE_KIND_UNDEF, goto ERR);

	uint32_t i;
	for(i = 0; i < 0xffff; i += 4u)
	{
		pss_value_t value = {
			.kind = PSS_VALUE_KIND_NUM,
			.num  = (int64_t)i * (int64_t)i 
		};
		ASSERT_OK(pss_frame_reg_set(frame, (pss_bytecode_regid_t)i, value), goto ERR);
	}
	
	for(i = 0; i < 0xffff - 4; i += 4u)
	{
		pss_value_const_t value = pss_frame_reg_get(frame, (pss_bytecode_regid_t)i);
		ASSERT(value.kind == PSS_VALUE_KIND_NUM, goto ERR);
		ASSERT(value.num == (int64_t)i * (int64_t)i, goto ERR);

		value = pss_frame_reg_get(frame, (pss_bytecode_regid_t)(i + 1));
		ASSERT(value.kind = PSS_VALUE_KIND_UNDEF, goto ERR);
	}
	
	ASSERT_OK(pss_frame_free(frame), CLEANUP_NOP);
	return 0;

ERR:

	ASSERT_OK(pss_frame_free(frame), CLEANUP_NOP);
	return ERROR_CODE(int);
}
int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(frame_test)
TEST_LIST_END;
