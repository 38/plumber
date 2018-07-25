/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <stdio.h>
#include <pss/log.h>
#include <pss/bytecode.h>

#define OPCODE(x) PSS_BYTECODE_OPCODE_##x

#define NUMERIC(x) PSS_BYTECODE_ARG_NUMERIC(x)

#define STRING(x) PSS_BYTECODE_ARG_STRING(x)

#define REG(x) PSS_BYTECODE_ARG_REGISTER(x)

#define LABEL(x) PSS_BYTECODE_ARG_LABEL(x)

#define END PSS_BYTECODE_ARG_END

char expected_inst[48][1024];

int code_generation_test(void)
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t regs[] = {2,1,4,3,0};

	uint32_t i = 0;
	for(i = 0; i < 128; i ++)
	{
		pss_bytecode_segment_t* segment = pss_bytecode_segment_new(5, regs);
		ASSERT_PTR(segment, CLEANUP_NOP);

		ASSERT(0 == pss_bytecode_segment_append_code(segment, OPCODE(INT_LOAD), NUMERIC(0x123), REG(10), END), CLEANUP_NOP);
		ASSERT(1 == pss_bytecode_segment_append_code(segment, OPCODE(STR_LOAD), STRING("hello"), REG(11), END), CLEANUP_NOP);
		ASSERT(2 == pss_bytecode_segment_append_code(segment, OPCODE(MOVE), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(3 == pss_bytecode_segment_append_code(segment, OPCODE(ADD), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(4 == pss_bytecode_segment_append_code(segment, OPCODE(SUB), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(5 == pss_bytecode_segment_append_code(segment, OPCODE(DIV), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(6 == pss_bytecode_segment_append_code(segment, OPCODE(MUL), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(7 == pss_bytecode_segment_append_code(segment, OPCODE(AND), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(8 == pss_bytecode_segment_append_code(segment, OPCODE(OR), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(9 == pss_bytecode_segment_append_code(segment, OPCODE(XOR), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(10 == pss_bytecode_segment_append_code(segment, OPCODE(DICT_NEW), REG(10), END), CLEANUP_NOP);
		ASSERT(11 == pss_bytecode_segment_append_code(segment, OPCODE(UNDEF_LOAD), REG(10), END), CLEANUP_NOP);
		ASSERT(12 == pss_bytecode_segment_append_code(segment, OPCODE(LENGTH), REG(10), REG(11), END), CLEANUP_NOP);
		ASSERT(13 == pss_bytecode_segment_append_code(segment, OPCODE(GET_VAL), REG(10), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(14 == pss_bytecode_segment_append_code(segment, OPCODE(SET_VAL), REG(10), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(15 == pss_bytecode_segment_append_code(segment, OPCODE(GET_KEY), REG(10), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(16 == pss_bytecode_segment_append_code(segment, OPCODE(GLOBAL_GET), REG(10), REG(11), END), CLEANUP_NOP);
		ASSERT(17 == pss_bytecode_segment_append_code(segment, OPCODE(GLOBAL_SET), REG(10), REG(11), END), CLEANUP_NOP);
		ASSERT(0 == pss_bytecode_segment_label_alloc(segment), CLEANUP_NOP);
		ASSERT(18 == pss_bytecode_segment_append_code(segment, OPCODE(INT_LOAD), LABEL(0), REG(0), END), CLEANUP_NOP);
		ASSERT_OK(pss_bytecode_segment_patch_label(segment, 0, 18), CLEANUP_NOP);
		ASSERT(19 == pss_bytecode_segment_append_code(segment, OPCODE(JUMP), REG(0), END), CLEANUP_NOP);
		ASSERT(20 == pss_bytecode_segment_append_code(segment, OPCODE(JZ), REG(0), REG(1), END), CLEANUP_NOP);
		ASSERT(21 == pss_bytecode_segment_append_code(segment, OPCODE(LT), REG(10), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(22 == pss_bytecode_segment_append_code(segment, OPCODE(EQ), REG(10), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(23 == pss_bytecode_segment_append_code(segment, OPCODE(CALL), REG(10), REG(12), END), CLEANUP_NOP);
		ASSERT(24 == pss_bytecode_segment_append_code(segment, OPCODE(CALL), REG(10), REG(12), END), CLEANUP_NOP);
		ASSERT(25 == pss_bytecode_segment_append_code(segment, OPCODE(CLOSURE_NEW), REG(0), REG(12), END), CLEANUP_NOP);
		ASSERT(26 == pss_bytecode_segment_append_code(segment, OPCODE(STR_LOAD), STRING("teststring1"), REG(12), END), CLEANUP_NOP);
		ASSERT(27 == pss_bytecode_segment_append_code(segment, OPCODE(STR_LOAD), STRING("teststring2"), REG(12), END), CLEANUP_NOP);
		ASSERT(28 == pss_bytecode_segment_append_code(segment, OPCODE(STR_LOAD), STRING("teststring3"), REG(12), END), CLEANUP_NOP);
		ASSERT(29 == pss_bytecode_segment_append_code(segment, OPCODE(STR_LOAD), STRING("teststring4"), REG(12), END), CLEANUP_NOP);
		ASSERT(30 == pss_bytecode_segment_append_code(segment, OPCODE(DINFO_LINE), NUMERIC(10), END), CLEANUP_NOP);
		ASSERT(31 == pss_bytecode_segment_append_code(segment, OPCODE(DINFO_FUNC), STRING("function@test.pss"), END), CLEANUP_NOP);
		ASSERT(32 == pss_bytecode_segment_append_code(segment, OPCODE(INT_LOAD), NUMERIC(0x123), REG(10), END), CLEANUP_NOP);
		ASSERT(33 == pss_bytecode_segment_append_code(segment, OPCODE(STR_LOAD), STRING("hello"), REG(11), END), CLEANUP_NOP);
		ASSERT(34 == pss_bytecode_segment_append_code(segment, OPCODE(MOVE), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(35 == pss_bytecode_segment_append_code(segment, OPCODE(ADD), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(36 == pss_bytecode_segment_append_code(segment, OPCODE(SUB), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(37 == pss_bytecode_segment_append_code(segment, OPCODE(DIV), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(38 == pss_bytecode_segment_append_code(segment, OPCODE(MUL), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(39 == pss_bytecode_segment_append_code(segment, OPCODE(AND), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(40 == pss_bytecode_segment_append_code(segment, OPCODE(OR), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(41 == pss_bytecode_segment_append_code(segment, OPCODE(XOR), REG(10), REG(12), REG(13), END), CLEANUP_NOP);
		ASSERT(42 == pss_bytecode_segment_append_code(segment, OPCODE(DICT_NEW), REG(10), END), CLEANUP_NOP);
		ASSERT(43 == pss_bytecode_segment_append_code(segment, OPCODE(UNDEF_LOAD), REG(10), END), CLEANUP_NOP);
		ASSERT(44 == pss_bytecode_segment_append_code(segment, OPCODE(LENGTH), REG(10), REG(11), END), CLEANUP_NOP);
		ASSERT(45 == pss_bytecode_segment_append_code(segment, OPCODE(GET_VAL), REG(10), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(46 == pss_bytecode_segment_append_code(segment, OPCODE(SET_VAL), REG(10), REG(11), REG(12), END), CLEANUP_NOP);
		ASSERT(47 == pss_bytecode_segment_append_code(segment, OPCODE(GET_KEY), REG(10), REG(11), REG(12), END), CLEANUP_NOP);

		uint32_t j;
		for(j =0; j < sizeof(expected_inst) / sizeof(expected_inst[0]); j ++)
			ASSERT_PTR(pss_bytecode_segment_inst_str(segment, (pss_bytecode_addr_t)j, expected_inst[j], sizeof(expected_inst[j])), CLEANUP_NOP);

		ASSERT_RETOK(pss_bytecode_segid_t, pss_bytecode_module_append(module, segment), CLEANUP_NOP);
	}

	ASSERT_OK(pss_bytecode_module_set_entry_point(module, 3), CLEANUP_NOP);

	ASSERT_OK(pss_bytecode_module_dump(module, TESTDIR"/test_code.psm"), CLEANUP_NOP);

	ASSERT_OK(pss_bytecode_module_logdump(module, NULL), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);
	return 0;
}

int module_load_test(void)
{
	pss_bytecode_module_t* module = NULL;
	ASSERT_PTR(module = pss_bytecode_module_load(TESTDIR"/test_code.psm"), CLEANUP_NOP);

	ASSERT(3 == pss_bytecode_module_get_entry_point(module), CLEANUP_NOP);

	pss_bytecode_segid_t i = 0;
	for(i = 0; i < 128; i ++)
	{
		const pss_bytecode_segment_t* segment = pss_bytecode_module_get_seg(module, i);
		ASSERT_PTR(segment, CLEANUP_NOP);

		pss_bytecode_regid_t const* args;
		ASSERT(5 == pss_bytecode_segment_get_args(segment, &args), CLEANUP_NOP);
		ASSERT(2 == args[0], CLEANUP_NOP);
		ASSERT(1 == args[1], CLEANUP_NOP);
		ASSERT(4 == args[2], CLEANUP_NOP);
		ASSERT(3 == args[3], CLEANUP_NOP);
		ASSERT(0 == args[4], CLEANUP_NOP);

		char buf[1024];
		uint32_t j;
		for(j = 0; j < sizeof(expected_inst) / sizeof(expected_inst[0]); j ++)
		{
			ASSERT_PTR(pss_bytecode_segment_inst_str(segment, (pss_bytecode_addr_t)j, buf, sizeof(buf)), CLEANUP_NOP);
			ASSERT_STREQ(buf, expected_inst[j], CLEANUP_NOP);
		}

		ASSERT(NULL == pss_bytecode_segment_inst_str(segment, (pss_bytecode_addr_t)j, buf, sizeof(buf)), CLEANUP_NOP);
	}

	ASSERT(NULL == pss_bytecode_module_get_seg(module, 128), CLEANUP_NOP);

	ASSERT_OK(pss_bytecode_module_logdump(module, NULL), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);
	return 0;
}


int setup(void)
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(code_generation_test),
    TEST_CASE(module_load_test)
TEST_LIST_END;
