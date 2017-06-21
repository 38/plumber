/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <pss.h>

pss_value_t run_module(pss_bytecode_module_t* module)
{
	pss_value_t result = {.kind = PSS_VALUE_KIND_ERROR};
	pss_vm_t* vm = pss_vm_new();
	if(NULL == vm) return result;
	if(ERROR_CODE(int) == pss_vm_run_module(vm, module, &result)) return result;

	if(ERROR_CODE(int) == pss_vm_free(vm)) return result;

	return result;
}

int test_primitive()
{
	char code [] = "return (function(x){" 
		           "	return function(y) { "
				   "		return function(z, g) {"
				   "			return g(x + y + z);"
				   "		}"
				   "	}" 
				   "}(11)(22)(33, function(x){"
				   "	return x * x;"
				   "}));";
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

	pss_value_t ret = run_module(opt.module);
	ASSERT(ret.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
	ASSERT(ret.num  == 66 * 66, CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(ret), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(opt.module), CLEANUP_NOP);

	return 0;
}

int test_gcd()
{
	char code [] = "gcd = function(a, b) {\n"
		           "    if(a == 0) {return b;}\n"
				   "    else {return gcd(b%a, a)}\n"
				   "};\n"
				   "return gcd(105, 45);\n";
	
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

	pss_value_t ret = run_module(opt.module);
	ASSERT(ret.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
	ASSERT(ret.num  == 15, CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(ret), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(opt.module), CLEANUP_NOP);

	return 0;
}

int high_order()
{
	char code [] = "timesN = function(a) {\n"
		           "    return function(b) {\n"
				   "    	return a * b;\n"
				   "	};\n"
				   "};\n"
				   "gen = function(x) {\n"
				   "	$global[\"times\" + x] = timesN(x);\n"
				   "};\n"
				   "gen(1);\n"
				   "gen(2);\n"
				   "a = times1(10) + times2(11)\n"
				   "b = 10;\n"
				   "while(b = b - 1) a = a + b;\n"
				   "return a;";
	
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

	pss_value_t ret = run_module(opt.module);
	ASSERT(ret.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
	ASSERT(ret.num  == 77, CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(ret), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(opt.module), CLEANUP_NOP);

	return 0;
}

int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);
	ASSERT_OK(pss_init(), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(test_primitive),
	TEST_CASE(test_gcd),
	TEST_CASE(high_order)
TEST_LIST_END;
