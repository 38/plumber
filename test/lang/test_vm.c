#include <testenv.h>
int test_fileserver()
{
	lang_vm_t* vm = NULL;
	const lang_compiler_error_t* ptr;
	int rc = ERROR_CODE(int);
	lang_lex_t* lexer = lang_lex_new("test_compiler_fileserver.in");
	lang_bytecode_table_t* bc = lang_bytecode_table_new();
	lang_compiler_options_t options = {
		.reg_limit = 65536
	};
	lang_compiler_t* compiler = lang_compiler_new(lexer, bc, options);
	ASSERT_PTR(lexer, goto ERR);
	ASSERT_PTR(bc, goto ERR);
	ASSERT_PTR(compiler, goto ERR);


	ASSERT_OK(lang_compiler_compile(compiler), goto ERR);

	ASSERT_OK(lang_bytecode_table_print(bc), goto ERR);

	ASSERT_OK(lang_compiler_validate(compiler), goto ERR);

	vm = lang_vm_new(bc);
	ASSERT_PTR(vm, goto ERR);

	ASSERT_OK(lang_vm_exec(vm), goto ERR);

	rc = 0;
ERR:
	for(ptr = lang_compiler_get_error(compiler); NULL != ptr; ptr = ptr->next)
	    LOG_ERROR("Compiler error at `%s' line %u offset %u: %s", ptr->file, ptr->line + 1, ptr->off + 1, ptr->message);
	if(NULL != lexer) lang_lex_free(lexer);
	if(NULL != bc) lang_bytecode_table_free(bc);
	if(NULL != compiler) lang_compiler_free(compiler);
	if(NULL != vm) lang_vm_free(vm);
	return rc;

}
int setup()
{
	expected_memory_leakage();
	ASSERT_OK(lang_lex_add_script_search_path(TESTDIR), CLEANUP_NOP);
	ASSERT_PTR(lang_lex_get_script_search_paths(), CLEANUP_NOP);
	ASSERT_PTR(lang_lex_get_script_search_paths()[0], CLEANUP_NOP);
	ASSERT_STREQ(lang_lex_get_script_search_paths()[0], TESTDIR, CLEANUP_NOP);
	ASSERT(lang_lex_get_num_script_search_paths() == 1, CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR"/../../examples/fileserver/"), CLEANUP_NOP);

	return 0;
}


DEFAULT_TEARDOWN;
TEST_LIST_BEGIN
    TEST_CASE(test_fileserver)
TEST_LIST_END;
