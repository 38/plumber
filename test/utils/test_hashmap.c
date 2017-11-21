#include <testenv.h>
#include <utils/hashmap.h>
#include <stdio.h>

int test_hashmap_new(void)
{
	hashmap_t* hm = hashmap_new(32767, 4096);
	ASSERT_PTR(hm, goto ERR);
	ASSERT_OK(hashmap_free(hm), goto ERR);
	return 0;
ERR:
	hashmap_free(hm);
	return ERROR_CODE(int);
}
int test_hashmap_insert_find(void)
{
	hashmap_t* hm = hashmap_new(32767, 4096);
	ASSERT_PTR(hm, goto ERR);
	int i;
	static char key[1024], val[1024];
	for(i = 0; i < 10240; i ++)
	{
		snprintf(key, sizeof(key), "%x this is the key for test key value pair #%d", i, i);
		snprintf(val, sizeof(val), "%d this is the value for the test key value pair #0x%x", i, i);
		ASSERT_OK(hashmap_insert(hm, key, strlen(key) + 1, val, strlen(val) + 1, NULL, 0), goto ERR);
	}

	for(i = 0; i < 10240; i ++)
	{
		snprintf(key, sizeof(key), "%x this is the key for test key value pair #%d", i, i);
		snprintf(val, sizeof(val), "%d this is the value for the test key value pair #0x%x", i, i);
		hashmap_find_res_t result;
		ASSERT(1 == hashmap_find(hm, key, strlen(key) + 1, &result), goto ERR);
		ASSERT_STREQ(key, (const char*)result.key_data, goto ERR);
		ASSERT_STREQ(val, (const char*)result.val_data, goto ERR);
		ASSERT(strlen(key) + 1 == result.key_size, goto ERR);
		ASSERT(strlen(val) + 1 == result.val_size, goto ERR);
	}

	hashmap_find_res_t result;

	ASSERT(0 == hashmap_find(hm, "12345678901234567890", 21, &result), goto ERR);
	ASSERT_OK(hashmap_free(hm), goto ERR);

	return 0;
ERR:
	hashmap_free(hm);
	return ERROR_CODE(int);
}
int setup(void)
{
	return 0;
}

int teardown(void)
{
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_hashmap_new),
    TEST_CASE(test_hashmap_insert_find)
TEST_LIST_END;
