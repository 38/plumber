/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <pss/log.h>

static int _called = 0;

void _write(int level, const char* file, const char* function, int line, const char* fmt, va_list pa)
{
	(void)level;
	(void)file;
	(void)function;
	(void)line;
	(void)fmt;
	(void)pa;
	_called = 1;
}

int test_pss_log_write(void)
{
	ASSERT(ERROR_CODE(int) == pss_log_set_write_callback(NULL), CLEANUP_NOP);
	pss_log_write(0, __FILE__, "func", __LINE__, "%s", "xxx");
	ASSERT(0 == _called, CLEANUP_NOP);

	ASSERT_OK(pss_log_set_write_callback(_write), CLEANUP_NOP);
	pss_log_write(0, __FILE__, "func", __LINE__, "%s", "xxx");
	ASSERT(1 == _called, CLEANUP_NOP);
	return 0;
}


DEFAULT_SETUP;

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_pss_log_write)
TEST_LIST_END;
