/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>

#include <testenv.h>
#include <pss.h>

int test_env(void)
{
	pss_comp_env_t* env = pss_comp_env_new();

	ASSERT_PTR(env, CLEANUP_NOP);

	pss_bytecode_regid_t regid;
	ASSERT_OK(pss_comp_env_open_scope(env), CLEANUP_NOP);
	ASSERT(0 == pss_comp_env_get_var(env, "global_var", 0, &regid), CLEANUP_NOP);
	ASSERT(1 == pss_comp_env_get_var(env, "local_var", 1, &regid), CLEANUP_NOP);
	ASSERT(pss_frame_serial_to_regid(0) == regid, CLEANUP_NOP);

	ASSERT_OK(pss_comp_env_open_scope(env), CLEANUP_NOP);
	ASSERT(1 == pss_comp_env_get_var(env, "local_var", 0, &regid), CLEANUP_NOP);
	ASSERT(pss_frame_serial_to_regid(0) == regid, CLEANUP_NOP);
	ASSERT(1 == pss_comp_env_get_var(env, "local_var", 1, &regid), CLEANUP_NOP);
	ASSERT(pss_frame_serial_to_regid(1) == regid, CLEANUP_NOP);
	ASSERT(ERROR_CODE(int) == pss_comp_env_get_var(env, "local_var", 1, &regid), CLEANUP_NOP);
	ASSERT(1 == pss_comp_env_get_var(env, "local_var", 0, &regid), CLEANUP_NOP);
	ASSERT(pss_frame_serial_to_regid(1) == regid, CLEANUP_NOP);
	ASSERT(1 == pss_comp_env_get_var(env, "a", 1, &regid), CLEANUP_NOP);
	ASSERT(1 == pss_comp_env_get_var(env, "b", 1, &regid), CLEANUP_NOP);
	ASSERT_OK(pss_comp_env_close_scope(env), CLEANUP_NOP);

	ASSERT(1 == pss_comp_env_get_var(env, "local_var", 0, &regid), CLEANUP_NOP);
	ASSERT(pss_frame_serial_to_regid(0) == regid, CLEANUP_NOP);

	ASSERT_OK(pss_comp_env_close_scope(env), CLEANUP_NOP);


	ASSERT_OK(pss_comp_env_free(env), CLEANUP_NOP);

	return 0;
}

int recur_test(void)
{
	static uint32_t level = 789;
	static pss_comp_env_t* env = NULL;
	level --;

	if(level == 0) goto RET;

	char buf[32];
	static pss_bytecode_regid_t varid[96];
	pss_bytecode_regid_t regid;

	if(env == NULL)
	{
		ASSERT_PTR(env = pss_comp_env_new(), CLEANUP_NOP);
		ASSERT_OK(pss_comp_env_open_scope(env), CLEANUP_NOP);
		ASSERT(1 == pss_comp_env_get_var(env, "top_level", 1, &regid), CLEANUP_NOP);
		ASSERT(pss_frame_serial_to_regid(0) == regid, CLEANUP_NOP);

		uint32_t i;
		for(i = 0; i < sizeof(varid) / sizeof(varid[0]); i ++)
		{
			snprintf(buf, sizeof(buf), "var%u", i);
			ASSERT(0 == pss_comp_env_get_var(env, buf, 0, varid + i), CLEANUP_NOP);
			ASSERT(1 == pss_comp_env_get_var(env, buf, 1, varid + i), CLEANUP_NOP);
		}
	}
	else
	{
		ASSERT_OK(pss_comp_env_open_scope(env), CLEANUP_NOP);
		uint32_t i;
		for(i = (level % 3); i < sizeof(varid) / sizeof(varid[0]); i += 3)
		{
			snprintf(buf, sizeof(buf), "var%u", i);
			ASSERT(1 == pss_comp_env_get_var(env, buf, 0, &regid), CLEANUP_NOP);
			ASSERT(regid == varid[i], CLEANUP_NOP);
			ASSERT(1 == pss_comp_env_get_var(env, buf, 1, varid + i), CLEANUP_NOP);
			ASSERT(varid[i] != regid, CLEANUP_NOP);
		}
	}
	uint32_t i, j;
	for(i = 0; i < sizeof(varid) / sizeof(varid[0]); i ++)
	    for(j = i + 1; j < sizeof(varid) / sizeof(varid[0]); j ++)
	        ASSERT(varid[i] != varid[j], CLEANUP_NOP);
	ASSERT(1 == pss_comp_env_get_var(env, "top_level", 0, &regid), CLEANUP_NOP);
	ASSERT(regid == pss_frame_serial_to_regid(0), CLEANUP_NOP);
	if(level % 200 == 0)
	{
		pss_bytecode_regid_t buf2[sizeof(varid) / sizeof(varid[0])];
		memcpy(buf2, varid, sizeof(buf2));
		ASSERT_OK(recur_test(), CLEANUP_NOP);
		memcpy(varid, buf2, sizeof(buf2));
		ASSERT_OK(recur_test(), CLEANUP_NOP);
		memcpy(varid, buf2, sizeof(buf2));
		ASSERT_OK(recur_test(), CLEANUP_NOP);
	}
	else
	    ASSERT_OK(recur_test(), CLEANUP_NOP);
	ASSERT_OK(pss_comp_env_close_scope(env), CLEANUP_NOP);
RET:
	level ++;
	if(level == 789) ASSERT_OK(pss_comp_env_free(env), CLEANUP_NOP);
	return 0;
}
int tmp_test(void)
{
	pss_comp_env_t* env;
	ASSERT_PTR(env = pss_comp_env_new(), CLEANUP_NOP);
	ASSERT_OK(pss_comp_env_open_scope(env), CLEANUP_NOP);
	pss_bytecode_regid_t regid, tmp, regid2, regid3;
	ASSERT(pss_comp_env_get_var(env, "myvar", 1, &regid) == 1, CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_regid_t, tmp = pss_comp_env_mktmp(env), CLEANUP_NOP);
	ASSERT(pss_comp_env_get_var(env, "myvar2", 1, &regid2) == 1, CLEANUP_NOP);
	ASSERT(regid != tmp, CLEANUP_NOP);
	ASSERT(regid2 != tmp, CLEANUP_NOP);
	ASSERT_OK(pss_comp_env_rmtmp(env, tmp), CLEANUP_NOP);
	ASSERT(pss_comp_env_get_var(env, "myvar3", 1, &regid3) == 1, CLEANUP_NOP);
	ASSERT(tmp == regid3, CLEANUP_NOP);
	ASSERT_OK(pss_comp_env_close_scope(env), CLEANUP_NOP);
	ASSERT_OK(pss_comp_env_free(env), CLEANUP_NOP);
	return 0;
}

static inline int _level(pss_bytecode_regid_t regid, pss_bytecode_regid_t left, pss_bytecode_regid_t right)
{
	if(right - left < 1) return 1;

	pss_bytecode_regid_t mid = (pss_bytecode_regid_t)((((unsigned)left) + ((unsigned)right)) / 2);

	if(regid == mid) return 1;
	if(regid < mid) return _level(regid, left, mid) + 1;
	else return _level(regid, mid, right) + 1;
}

int regsn_test(void)
{
	int count[0xffff] = {};
	pss_bytecode_regid_t i;
	int last_level = 0;
	for(i = 0; i < 0xffff; i ++)
	{
		ASSERT(pss_frame_regid_to_serial(pss_frame_serial_to_regid(i)) == i, CLEANUP_NOP);
		int this_level = _level(pss_frame_serial_to_regid(i), 0, 0xffff);
		ASSERT(this_level >= last_level, CLEANUP_NOP);
		count[pss_frame_serial_to_regid(i)] ++;
		last_level = this_level;
	}

	for(i = 0; i < 0xffff; i ++)
	    ASSERT(count[i] == 1, CLEANUP_NOP);

	return 0;
}
int setup(void)
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_env),
    TEST_CASE(recur_test),
    TEST_CASE(tmp_test),
    TEST_CASE(regsn_test)
TEST_LIST_END;
