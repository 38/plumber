/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <pss.h>

int test_primitive()
{
	char code [] = "function(x,y){ return function(z) { return x; } }(2,3)(4)[5][6];";
	pss_comp_lex_t* lex = pss_comp_lex_new("<code>", code, sizeof(code));
	ASSERT_PTR(lex, CLEANUP_NOP);

	pss_comp_option_t opt = {
		.module = pss_bytecode_module_new(),
		.lexer = lex
	};
	pss_comp_error_t* error;

	ASSERT_OK(pss_comp_compile(&opt, &error), CLEANUP_NOP);

	ASSERT_OK(pss_bytecode_module_logdump(opt.module), CLEANUP_NOP);

	ASSERT_OK(pss_comp_lex_free(lex), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(opt.module), CLEANUP_NOP);

	return 0;
}

int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(test_primitive)
TEST_LIST_END;
