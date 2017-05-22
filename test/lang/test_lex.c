/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
lang_lex_t* lexer = NULL;
int search_path()
{
	ASSERT_OK(lang_lex_add_script_search_path(TESTDIR), CLEANUP_NOP);
	ASSERT_PTR(lang_lex_get_script_search_paths(), CLEANUP_NOP);
	ASSERT_PTR(lang_lex_get_script_search_paths()[0], CLEANUP_NOP);
	ASSERT_STREQ(lang_lex_get_script_search_paths()[0], TESTDIR, CLEANUP_NOP);
	ASSERT(lang_lex_get_num_script_search_paths() == 1, CLEANUP_NOP);
	return 0;
}
int create_lexer()
{
	ASSERT_PTR(lexer = lang_lex_new("test_lang.in"), CLEANUP_NOP);

	return 0;
}
int verify_token()
{
	lang_lex_token_t token;
	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_IDENTIFIER, CLEANUP_NOP);
	ASSERT_STREQ(token.value.s, "set", CLEANUP_NOP);
	ASSERT(token.line == 6, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_KEYWORD && token.value.k == LANG_LEX_KEYWORD_ECHO, CLEANUP_NOP);
	ASSERT(token.line == 6, CLEANUP_NOP);
	ASSERT(token.offset == 21, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);


	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_KEYWORD && token.value.k == LANG_LEX_KEYWORD_VISUALIZE, CLEANUP_NOP);
	ASSERT(token.line == 6, CLEANUP_NOP);
	ASSERT(token.offset == 26, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_KEYWORD && token.value.k == LANG_LEX_KEYWORD_START, CLEANUP_NOP);
	ASSERT(token.line == 6, CLEANUP_NOP);
	ASSERT(token.offset == 36, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_KEYWORD && token.value.k == LANG_LEX_KEYWORD_INCLUDE, CLEANUP_NOP);
	ASSERT(token.line == 7, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_STRING, CLEANUP_NOP);
	ASSERT_STREQ(token.value.s, "testfile\rAB", CLEANUP_NOP);
	ASSERT(token.line == 7, CLEANUP_NOP);
	ASSERT(token.offset == 8, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_IDENTIFIER, CLEANUP_NOP);
	ASSERT_STREQ(token.value.s, "$a123_456_ABC", CLEANUP_NOP);
	ASSERT(token.line == 8, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_EQUAL, CLEANUP_NOP);
	ASSERT(token.line == 8, CLEANUP_NOP);
	ASSERT(token.offset == 14, CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_INTEGER && token.value.i == 223, CLEANUP_NOP);
	ASSERT(token.line == 8, CLEANUP_NOP);
	ASSERT(token.offset == 16, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_INTEGER && token.value.i == 0xff, CLEANUP_NOP);
	ASSERT(token.line == 9, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_include_script(lexer, "test_lang_inc.in"), CLEANUP_NOP);
	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_INTEGER, CLEANUP_NOP);
	ASSERT(token.value.i == 12345, CLEANUP_NOP);
	ASSERT(token.line == 1, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang_inc.in", CLEANUP_NOP);
	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_EOF, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang_inc.in", CLEANUP_NOP);
	ASSERT_OK(lang_lex_pop_include_script(lexer), CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_LBRACE, CLEANUP_NOP);
	ASSERT(token.line == 10, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_RBRACE, CLEANUP_NOP);
	ASSERT(token.line == 10, CLEANUP_NOP);
	ASSERT(token.offset == 1, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_include_script(lexer, "test_lang_inc.in"), CLEANUP_NOP);
	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_INTEGER, CLEANUP_NOP);
	ASSERT(token.value.i == 12345, CLEANUP_NOP);
	ASSERT(token.line == 1, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang_inc.in", CLEANUP_NOP);
	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_EOF, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang_inc.in", CLEANUP_NOP);
	ASSERT_OK(lang_lex_pop_include_script(lexer), CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_EQUAL, CLEANUP_NOP);
	ASSERT(token.line == 11, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_LPARENTHESIS, CLEANUP_NOP);
	ASSERT(token.line == 12, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_RPARENTHESIS, CLEANUP_NOP);
	ASSERT(token.line == 13, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_GRAPHVIZ_PROP, CLEANUP_NOP);
	ASSERT(token.line == 14, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.value.s, "\n", CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_LBRACE, CLEANUP_NOP);
	ASSERT(token.line == 16, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_RBRACE, CLEANUP_NOP);
	ASSERT(token.line == 17, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);


	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_LT, CLEANUP_NOP);
	ASSERT(token.line == 18, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_GT, CLEANUP_NOP);
	ASSERT(token.line == 19, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_SEMICOLON, CLEANUP_NOP);
	ASSERT(token.line == 20, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_DOT, CLEANUP_NOP);
	ASSERT(token.line == 21, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_COLON_EQUAL, CLEANUP_NOP);
	ASSERT(token.line == 22, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_ARROW, CLEANUP_NOP);
	ASSERT(token.line == 23, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_INTEGER, CLEANUP_NOP);
	ASSERT(token.value.i == 0377, CLEANUP_NOP);
	ASSERT(token.line == 24, CLEANUP_NOP);
	ASSERT(token.offset == 0, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);

	ASSERT_OK(lang_lex_next_token(lexer, &token), CLEANUP_NOP);
	ASSERT(token.type == LANG_LEX_TOKEN_EOF, CLEANUP_NOP);
	ASSERT_STREQ(token.file, TESTDIR"/test_lang.in", CLEANUP_NOP);
	return 0;
}
/* TODO test other tokens */
int loop_include()
{
	ASSERT(lang_lex_include_script(lexer, "test_lang.in") == ERROR_CODE(int), CLEANUP_NOP);
	return 0;
}
int setup()
{
	return 0;
}

int teardown()
{
	ASSERT_OK(lang_lex_include_script(lexer, "test_lang_inc.in"), CLEANUP_NOP);
	ASSERT_OK(lang_lex_free(lexer), CLEANUP_NOP);
	return 0;
}
TEST_LIST_BEGIN
    TEST_CASE(search_path),
    TEST_CASE(create_lexer),
    TEST_CASE(verify_token),
    TEST_CASE(loop_include)
TEST_LIST_END;
