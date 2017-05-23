/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <utils/string.h>
#include <stdlib.h>
int test_string_buffer()
{
	char buffer[20];
	string_buffer_t b;
	string_buffer_open(buffer, sizeof(buffer), &b);
	ASSERT(string_buffer_append("this ", &b) == 5, CLEANUP_NOP);
	ASSERT(string_buffer_append("",&b) == 0, CLEANUP_NOP);
	ASSERT(string_buffer_append("is ", &b) == 3, CLEANUP_NOP);
	ASSERT(string_buffer_append("a test!", &b) == 7, CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&b), "this is a test!", CLEANUP_NOP);
	return 0;
}
int test_string_buffer_truncate()
{
	char buffer[10];
	string_buffer_t b;
	string_buffer_open(buffer, sizeof(buffer), &b);
	ASSERT(string_buffer_append("12345678", &b) == 8, CLEANUP_NOP);
	ASSERT(string_buffer_append("901234567", &b) == 1, CLEANUP_NOP);
	ASSERT(string_buffer_append("12345", &b) == 0, CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&b), "123456789", CLEANUP_NOP);
	return 0;
}

int test_string_buffer_appendf()
{
	char buffer[20];
	string_buffer_t b;
	string_buffer_open(buffer, sizeof(buffer), &b);
	ASSERT(string_buffer_appendf(&b, "%d", 12345) == 5, CLEANUP_NOP);
	ASSERT(string_buffer_appendf(&b, "%s", "67890") == 5, CLEANUP_NOP);
	ASSERT(string_buffer_appendf(&b, "%x", 0x12345) == 5, CLEANUP_NOP);
	ASSERT(string_buffer_appendf(&b, "%x", 0xabcdef) == 4, CLEANUP_NOP);
	ASSERT(string_buffer_appendf(&b, "test") == 0, CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&b), "123456789012345abcd", CLEANUP_NOP);

	return 0;
}
int test_string_buffer_range()
{
	char buffer[20];
	string_buffer_t b;
	string_buffer_open(buffer, sizeof(buffer), &b);
	const char str[] = "123456789012345678901234567890";
	ASSERT(string_buffer_append_range(str, str + sizeof(str) - 1, &b) == 19, CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&b), "1234567890123456789", CLEANUP_NOP);

	return 0;
}

int test_string_buffer_empty()
{
	char buffer[10];
	string_buffer_t b;
	string_buffer_open(buffer, sizeof(buffer), &b);
	ASSERT_STREQ(string_buffer_close(&b), "", CLEANUP_NOP);
	return 0;
}

DEFAULT_SETUP;
DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_string_buffer),
    TEST_CASE(test_string_buffer_truncate),
    TEST_CASE(test_string_buffer_empty),
    TEST_CASE(test_string_buffer_appendf),
    TEST_CASE(test_string_buffer_range)
TEST_LIST_END;
