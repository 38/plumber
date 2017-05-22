/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <utils/string.h>

static lang_bytecode_table_t* tab = NULL;

int table_creation()
{
	ASSERT_PTR(tab = lang_bytecode_table_new(), CLEANUP_NOP);
	ASSERT(0 == lang_bytecode_table_get_num_regs(tab), CLEANUP_NOP);
	return 0;
}

int add_move_reg_str()
{
	lang_bytecode_operand_t left = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg  = 0
	};
	lang_bytecode_operand_t right = {
		.type = LANG_BYTECODE_OPERAND_STR,
		.str = "this is a test string"
	};
	ASSERT_OK(lang_bytecode_table_append_move(tab, &left, &right), CLEANUP_NOP);
	return 0;
}
int verify_move_reg_str()
{
	uint32_t offset = 0;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_MOVE, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1, op2;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 2, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 0, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op1.id == 0, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 1, &op2), CLEANUP_NOP);
	ASSERT(op2.type == LANG_BYTECODE_OPERAND_STR, CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_str_id_to_string(tab, op2), "this is a test string", CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "move R0, (string)\"this is a test string\"", CLEANUP_NOP);
	return 0;
}
int add_move_reg_graphviz()
{
	lang_bytecode_operand_t left = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg  = 0
	};
	lang_bytecode_operand_t right = {
		.type = LANG_BYTECODE_OPERAND_GRAPHVIZ,
		.str = "[shape = box]"
	};
	ASSERT_OK(lang_bytecode_table_append_move(tab, &left, &right), CLEANUP_NOP);
	return 0;
}
int verify_move_reg_graphviz()
{
	uint32_t offset = 1;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_MOVE, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1, op2;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 2, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 0, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op1.id == 0, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 1, &op2), CLEANUP_NOP);
	ASSERT(op2.type == LANG_BYTECODE_OPERAND_GRAPHVIZ, CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_str_id_to_string(tab, op2), "[shape = box]", CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "move R0, (graphviz)\"[shape = box]\"", CLEANUP_NOP);
	return 0;
}
int add_move_reg_num()
{
	lang_bytecode_operand_t left = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg  = 0
	};
	lang_bytecode_operand_t right = {
		.type = LANG_BYTECODE_OPERAND_INT,
		.num = 123
	};
	ASSERT_OK(lang_bytecode_table_append_move(tab, &left, &right), CLEANUP_NOP);
	return 0;
}
int verify_move_reg_num()
{
	uint32_t offset = 2;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_MOVE, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1, op2;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 2, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 0, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op1.id == 0, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 1, &op2), CLEANUP_NOP);
	ASSERT(op2.type == LANG_BYTECODE_OPERAND_INT, CLEANUP_NOP);
	ASSERT(op2.num == 123, CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "move R0, 123", CLEANUP_NOP);
	return 0;
}
int add_move_reg_sym()
{
	lang_bytecode_operand_t left = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg  = 0
	};
	uint32_t symtab[4]; //{"module", "tcp", "port", NULL};
	ASSERT_RETOK(uint32_t, symtab[0] = lang_bytecode_table_acquire_string_id(tab, "module"), CLEANUP_NOP);
	ASSERT_RETOK(uint32_t, symtab[1] = lang_bytecode_table_acquire_string_id(tab, "tcp"), CLEANUP_NOP);
	ASSERT_RETOK(uint32_t, symtab[2] = lang_bytecode_table_acquire_string_id(tab, "port"), CLEANUP_NOP);
	symtab[3] = ERROR_CODE(uint32_t);

	lang_bytecode_operand_t right = {
		.type = LANG_BYTECODE_OPERAND_SYM,
		.sym = symtab
	};
	ASSERT_OK(lang_bytecode_table_append_move(tab, &left, &right), CLEANUP_NOP);
	return 0;
}
int verify_move_reg_sym()
{
	uint32_t offset = 3;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_MOVE, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1, op2;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 2, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 0, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op1.id == 0, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 1, &op2), CLEANUP_NOP);
	ASSERT(op2.type == LANG_BYTECODE_OPERAND_SYM, CLEANUP_NOP);
	ASSERT(3 == lang_bytecode_table_sym_id_length(tab, op2), CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_sym_id_to_string(tab, op2, 0), "module", CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_sym_id_to_string(tab, op2, 1), "tcp", CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_sym_id_to_string(tab, op2, 2), "port", CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "move R0, module.tcp.port", CLEANUP_NOP);
	return 0;
}
int add_move_reg_reg()
{
	lang_bytecode_operand_t left = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg  = 0
	};
	lang_bytecode_operand_t right = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg = 1
	};
	ASSERT_OK(lang_bytecode_table_append_move(tab, &left, &right), CLEANUP_NOP);
	return 0;
}
int verify_move_reg_reg()
{
	uint32_t offset = 4;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_MOVE, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1, op2;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 2, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 0, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op1.id == 0, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 1, &op2), CLEANUP_NOP);
	ASSERT(op2.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op2.id == 1, CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "move R0, R1", CLEANUP_NOP);
	return 0;
}
int add_move_sym_reg()
{
	uint32_t symtab[4];// = {"module", "tcp", "ttl", NULL};
	ASSERT_RETOK(uint32_t, symtab[0] = lang_bytecode_table_acquire_string_id(tab, "module"), CLEANUP_NOP);
	ASSERT_RETOK(uint32_t, symtab[1] = lang_bytecode_table_acquire_string_id(tab, "tcp"), CLEANUP_NOP);
	ASSERT_RETOK(uint32_t, symtab[2] = lang_bytecode_table_acquire_string_id(tab, "ttl"), CLEANUP_NOP);
	symtab[3] = ERROR_CODE(uint32_t);

	lang_bytecode_operand_t left = {
		.type = LANG_BYTECODE_OPERAND_SYM,
		.sym  = symtab
	};
	lang_bytecode_operand_t right = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg = 1
	};
	ASSERT_OK(lang_bytecode_table_append_move(tab, &left, &right), CLEANUP_NOP);
	return 0;
}
int verify_move_sym_reg()
{
	uint32_t offset = 5;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_MOVE, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1, op2;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 2, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 1, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op1.id == 1, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 0, &op2), CLEANUP_NOP);
	ASSERT(op2.type == LANG_BYTECODE_OPERAND_SYM, CLEANUP_NOP);
	ASSERT(3 == lang_bytecode_table_sym_id_length(tab, op2), CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_sym_id_to_string(tab, op2, 0), "module", CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_sym_id_to_string(tab, op2, 1), "tcp", CLEANUP_NOP);
	ASSERT_STREQ(lang_bytecode_table_sym_id_to_string(tab, op2, 2), "ttl", CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "move module.tcp.ttl, R1", CLEANUP_NOP);
	return 0;
}
static inline int _test_invalid_combination(lang_bytecode_operand_type_t a, lang_bytecode_operand_type_t b)
{
	lang_bytecode_operand_t left = {
		.type = a,
		.reg  = 0
	};
	lang_bytecode_operand_t right = {
		.type = b,
		.reg = 1
	};
	ASSERT(ERROR_CODE(int) == lang_bytecode_table_append_move(tab, &left, &right), CLEANUP_NOP);

	return 0;
}
int add_move_invalid()
{
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_STR, LANG_BYTECODE_OPERAND_STR), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_STR, LANG_BYTECODE_OPERAND_REG), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_STR, LANG_BYTECODE_OPERAND_INT), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_STR, LANG_BYTECODE_OPERAND_GRAPHVIZ), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_STR, LANG_BYTECODE_OPERAND_BUILTIN), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_STR, LANG_BYTECODE_OPERAND_SYM), CLEANUP_NOP);

	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_GRAPHVIZ, LANG_BYTECODE_OPERAND_STR), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_GRAPHVIZ, LANG_BYTECODE_OPERAND_REG), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_GRAPHVIZ, LANG_BYTECODE_OPERAND_INT), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_GRAPHVIZ, LANG_BYTECODE_OPERAND_GRAPHVIZ), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_GRAPHVIZ, LANG_BYTECODE_OPERAND_BUILTIN), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_GRAPHVIZ, LANG_BYTECODE_OPERAND_SYM), CLEANUP_NOP);

	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_INT, LANG_BYTECODE_OPERAND_STR), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_INT, LANG_BYTECODE_OPERAND_REG), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_INT, LANG_BYTECODE_OPERAND_INT), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_INT, LANG_BYTECODE_OPERAND_GRAPHVIZ), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_INT, LANG_BYTECODE_OPERAND_BUILTIN), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_INT, LANG_BYTECODE_OPERAND_SYM), CLEANUP_NOP);

	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_BUILTIN, LANG_BYTECODE_OPERAND_STR), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_BUILTIN, LANG_BYTECODE_OPERAND_REG), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_BUILTIN, LANG_BYTECODE_OPERAND_INT), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_BUILTIN, LANG_BYTECODE_OPERAND_GRAPHVIZ), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_BUILTIN, LANG_BYTECODE_OPERAND_BUILTIN), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_BUILTIN, LANG_BYTECODE_OPERAND_SYM), CLEANUP_NOP);

	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_SYM, LANG_BYTECODE_OPERAND_STR), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_SYM, LANG_BYTECODE_OPERAND_INT), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_SYM, LANG_BYTECODE_OPERAND_GRAPHVIZ), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_SYM, LANG_BYTECODE_OPERAND_BUILTIN), CLEANUP_NOP);
	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_SYM, LANG_BYTECODE_OPERAND_SYM), CLEANUP_NOP);

	ASSERT_OK(_test_invalid_combination(LANG_BYTECODE_OPERAND_REG, LANG_BYTECODE_OPERAND_BUILTIN), CLEANUP_NOP);
	return 0;
}
int add_pusharg_reg()
{
	lang_bytecode_operand_t op = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg = 0
	};
	ASSERT_OK(lang_bytecode_table_append_pusharg(tab, &op), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_STR;
	ASSERT(lang_bytecode_table_append_pusharg(tab, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_INT;
	ASSERT(lang_bytecode_table_append_pusharg(tab, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_GRAPHVIZ;
	ASSERT(lang_bytecode_table_append_pusharg(tab, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_SYM;
	ASSERT(lang_bytecode_table_append_pusharg(tab, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_BUILTIN;
	ASSERT(lang_bytecode_table_append_pusharg(tab, &op) == ERROR_CODE(int), CLEANUP_NOP);
	return 0;
}

int verify_pusharg()
{
	uint32_t offset = 6;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_PUSHARG, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 1, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 0, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_REG, CLEANUP_NOP);
	ASSERT(op1.id == 0, CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "pusharg R0", CLEANUP_NOP);
	return 0;
}

int add_invoke_builtin()
{

	lang_bytecode_operand_t reg = {
		.type = LANG_BYTECODE_OPERAND_REG,
		.reg  = 0
	};
	lang_bytecode_operand_t op = {
		.type = LANG_BYTECODE_OPERAND_BUILTIN,
		.builtin = LANG_BYTECODE_BUILTIN_NEW_GRAPH
	};
	ASSERT_OK(lang_bytecode_table_append_invoke(tab, &reg, &op), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_STR;
	ASSERT(lang_bytecode_table_append_invoke(tab, &reg, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_INT;
	ASSERT(lang_bytecode_table_append_invoke(tab, &reg, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_GRAPHVIZ;
	ASSERT(lang_bytecode_table_append_invoke(tab, &reg, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_SYM;
	ASSERT(lang_bytecode_table_append_invoke(tab, &reg, &op) == ERROR_CODE(int), CLEANUP_NOP);

	op.type = LANG_BYTECODE_OPERAND_REG;
	ASSERT(lang_bytecode_table_append_invoke(tab, &reg, &op) == ERROR_CODE(int), CLEANUP_NOP);
	return 0;
}
int verify_invoke_builtin()
{
	uint32_t offset = 7;
	ASSERT(lang_bytecode_table_get_opcode(tab, offset) == LANG_BYTECODE_OPCODE_INVOKE, CLEANUP_NOP);
	lang_bytecode_operand_id_t op1;
	ASSERT(lang_bytecode_table_get_num_operand(tab, offset) == 2, CLEANUP_NOP);
	ASSERT_OK(lang_bytecode_table_get_operand(tab, offset, 1, &op1), CLEANUP_NOP);
	ASSERT(op1.type == LANG_BYTECODE_OPERAND_BUILTIN, CLEANUP_NOP);
	ASSERT(op1.id == LANG_BYTECODE_BUILTIN_NEW_GRAPH, CLEANUP_NOP);
	char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);
	ASSERT_OK(lang_bytecode_table_append_to_string_buffer(tab, offset, &sb), CLEANUP_NOP);
	ASSERT_STREQ(string_buffer_close(&sb), "invoke R0, __builtin_new_graph", CLEANUP_NOP);
	return 0;
}
DEFAULT_SETUP;

int teardown()
{
	if(NULL != tab)
	{
		ASSERT_OK(lang_bytecode_table_free(tab), CLEANUP_NOP);
	}
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(table_creation),
    TEST_CASE(add_move_reg_str),
    TEST_CASE(add_move_reg_graphviz),
    TEST_CASE(add_move_reg_num),
    TEST_CASE(add_move_reg_sym),
    TEST_CASE(add_move_reg_reg),
    TEST_CASE(add_move_sym_reg),
    TEST_CASE(add_move_invalid),
    TEST_CASE(add_pusharg_reg),
    TEST_CASE(add_invoke_builtin),
    TEST_CASE(verify_move_reg_str),
    TEST_CASE(verify_move_reg_graphviz),
    TEST_CASE(verify_move_reg_num),
    TEST_CASE(verify_move_reg_sym),
    TEST_CASE(verify_move_reg_reg),
    TEST_CASE(verify_move_sym_reg),
    TEST_CASE(verify_pusharg),
    TEST_CASE(verify_invoke_builtin)
TEST_LIST_END;
