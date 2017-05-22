/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The bytecode for the Plumber Service Script (PSS)
 * @file lang/bytecode.h
 **/
#ifndef __PLUMBER_LANG_BYTECODE_H__
#define __PLUMBER_LANG_BYTECODE_H__
#include <utils/string.h>
#include <utils/static_assertion.h>
/**
 * @brief the opcode for a bytecode
 **/
typedef enum {
	LANG_BYTECODE_OPCODE_MOVE,      /*!< move: move a value to another */
	LANG_BYTECODE_OPCODE_INVOKE,    /*!< invoke: invoke a function */
	LANG_BYTECODE_OPCODE_PUSHARG,   /*!< push a argument to the argument list */
	LANG_BYTECODE_OPCODE_ADD,       /*!< reg_out = reg_in_1 + reg_in_2 */
	LANG_BYTECODE_OPCODE_SUB,       /*!< reg_out = reg_in_1 - reg_in_2 */
	LANG_BYTECODE_OPCODE_MUL,       /*!< reg_out = reg_in_1 * reg_in_2 */
	LANG_BYTECODE_OPCODE_DIV,       /*!< reg_out = reg_in_1 / reg_in_2 */
	LANG_BYTECODE_OPCODE_MOD,       /*!< reg_out = reg_in_1 % reg_in_2 */
	LANG_BYTECODE_OPCODE_AND,       /*!< reg_out = reg_in_1 && reg_in_2 */
	LANG_BYTECODE_OPCODE_OR,        /*!< reg_out = reg_in_1 && reg_in_2 */
	LANG_BYTECODE_OPCODE_XOR,       /*!< reg_out = reg_in_1 ^ reg_in_2 */
	LANG_BYTECODE_OPCODE_EQ,        /*!< reg_out = reg_in_1 == reg_in_2 */
	LANG_BYTECODE_OPCODE_NE,        /*!< reg_out = reg_in_1 != reg_in_2 */
	LANG_BYTECODE_OPCODE_GT,        /*!< reg_out = reg_in_1 &gt;= reg_in_2 */
	LANG_BYTECODE_OPCODE_GE,        /*!< reg_out = reg_in_1 &gt; reg_in_2 */
	LANG_BYTECODE_OPCODE_LT,        /*!< reg_out = reg_in_1 &lt;= reg_in_2 */
	LANG_BYTECODE_OPCODE_LE,        /*!< reg_out = reg_in_1 &lt; reg_in_2 */
	LANG_BYTECODE_OPCODE_JUMP,      /*!< jump to address reg_in */
	LANG_BYTECODE_OPCODE_JZ,        /*!< if(reg_in_1 == 0) jump reg_in_2 */
	LANG_BYTECODE_OPCODE_UNDEFINED, /*!< reg_out = undefined */
	LANG_BYTECODE_OPCODE_COUNT      /*!< the number of opcodes */
	/* DO NOT ADD CODE HERE */
} lang_bytecode_opcode_t;
/* we use 8 bit for this */
STATIC_ASSERTION_LE(LANG_BYTECODE_OPCODE_COUNT, 255);

/**
 * @brief the type code for a bytecode operand
 **/
typedef enum {
	LANG_BYTECODE_OPERAND_SYM,      /*!< a symbol reference */
	LANG_BYTECODE_OPERAND_REG,      /*!< a register reference*/
	LANG_BYTECODE_OPERAND_STR,      /*!< a string instant */
	LANG_BYTECODE_OPERAND_INT,      /*!< a integer instant */
	LANG_BYTECODE_OPERAND_GRAPHVIZ, /*!< a graphviz property */
	LANG_BYTECODE_OPERAND_BUILTIN,  /*!< a builtin function */
	LANG_BYTECODE_OPERAND_COUNT,    /*!< the number of types */
	LANG_BYTECODE_OPERAND_LABEL,    /*!< the labels, NOTE: this is not actually a valid operand, but used for code generation, use lang_bytecode_patch to remove all such instruction with a label table */
	LANG_BYTECODE_OPERAND_STRID/*!< indicates that we use a string id refer the string, this is not actually a type of operand, but used as an additional interface*/
	/* DO NOT ADD CODE HERE */
} lang_bytecode_operand_type_t;
STATIC_ASSERTION_LE(LANG_BYTECODE_OPERAND_COUNT, 255);

/**
 * @brief the builtin function type
 **/
typedef enum {
	LANG_BYTECODE_BUILTIN_NEW_GRAPH,    /*!< create a new graph */
	LANG_BYTECODE_BUILTIN_ADD_NODE,     /*!< add a node to the graph */
	LANG_BYTECODE_BUILTIN_ADD_EDGE,     /*!< add a new edge to the graph */
	LANG_BYTECODE_BUILTIN_ECHO,         /*!< output a message to screen */
	LANG_BYTECODE_BUILTIN_GRAPHVIZ,     /*!< generate visualization of service graph */
	LANG_BYTECODE_BUILTIN_START,        /*!< start a service */
	LANG_BYTECODE_BUILTIN_INPUT,        /*!< set the input of a service graph */
	LANG_BYTECODE_BUILTIN_OUTPUT,       /*!< set the output of a service grpah */
	LANG_BYTECODE_BUILTIN_INSMOD        /*!< insert a new module instance to module table */
} lang_bytecode_builtin_t;

/**
 * @brief the general type for a operand
 **/
typedef struct {
	lang_bytecode_operand_type_t type;  /*!< the type of the operand */
	union {
		uint32_t label;                 /*!< the label id */
		uint32_t reg;                   /*!< the register id */
		int32_t num;                    /*!< the integer type */
		lang_bytecode_builtin_t builtin;/*!< the builtin function */
		const char* str;                /*!< the string literal */
		const char* graphviz;           /*!< the graphviz code */
		uint32_t const* sym;            /*!< the symbol reference */
		uint32_t strid;                 /*!< the string id */
	};
} lang_bytecode_operand_t;

/**
 * @brief the operand id
 **/
typedef struct {
	lang_bytecode_operand_type_t type; /*!< the type of this operand */
	union {
		uint32_t id;     /*!< represents a operand id */
		int32_t  num;    /*!< represents a number */
	};
} lang_bytecode_operand_id_t;

/**
 * @brief the incomplete type for a bytecode table, which contains all the bytecodes
 *        for a script
 **/
typedef struct _lang_bytecode_table_t lang_bytecode_table_t;

/**
 * @brief the label table
 **/
typedef struct _lang_bytecode_label_table_t lang_bytecode_label_table_t;

/**
 * @brief create a new bytecode table
 * @note this function is used for both code generation and image load
 * @return the newly created bytecode table
 **/
lang_bytecode_table_t* lang_bytecode_table_new();

/**
 * @brief dispose the used bytecode table
 * @param table the used bytecode table
 * @return status code
 **/
int lang_bytecode_table_free(lang_bytecode_table_t* table);

/**
 * @brief get the number of registers is required by this script
 * @param table the target bytecode table
 * @return the number of register or status code
 **/
uint32_t lang_bytecode_table_get_num_regs(const lang_bytecode_table_t* table);

/**
 * @brief append a move Reg, Str instruction
 * @param table the target bytecode table
 * @param src the source operand
 * @param dest the dest operand
 * @return status code
 **/
int lang_bytecode_table_append_move(lang_bytecode_table_t* table, const lang_bytecode_operand_t* dest, const lang_bytecode_operand_t* src);

/**
 * @brief append invoke instrunction, which means call a function with the previously construction arg
 * @param table the target bytecode table
 * @param reg the target register
 * @param func the target function
 * @return the status code
 **/
int lang_bytecode_table_append_invoke(lang_bytecode_table_t* table, const lang_bytecode_operand_t* reg, const lang_bytecode_operand_t* func);

/**
 * @brief append a jump instruction
 * @param table the bytecode table
 * @param target the operand indicates the target address
 * @returns status code
 **/
int lang_bytecode_table_append_jump(lang_bytecode_table_t* table, const lang_bytecode_operand_t* target);

/**
 * @brief append a jump instruction
 * @param table the bytecode table
 * @param cond the condition operands
 * @param target the operand indicates the target address
 * @returns status code
 **/
int lang_bytecode_table_append_jz(lang_bytecode_table_t* table, const lang_bytecode_operand_t* cond, const lang_bytecode_operand_t* target);

/**
 * @brief append pusharg instruction, which push a *reference of register* to the argument list
 * @param table the target bytecode table
 * @param reg the argument
 * @return the status code
 **/
int lang_bytecode_table_append_pusharg(lang_bytecode_table_t* table, const lang_bytecode_operand_t* reg);

/**
 * @brief append a arithmetic instruction to the buffer
 * @note this means dest <- left op right
 * @param table the target byte code table
 * @param opcode the opcode to add
 * @param dest the destination register
 * @param left the left operand
 * @param right the right operand
 * @return stauts code
 **/
int lang_bytecode_table_append_arithmetic(lang_bytecode_table_t* table, lang_bytecode_opcode_t opcode,
                                          const lang_bytecode_operand_t* dest, const lang_bytecode_operand_t* left,
                                          const lang_bytecode_operand_t* right);

/**
 * @brief append a moveundef instruction to the bytecode table
 * @param table the target bytecode table
 * @param dest the destination
 * @return status code
 **/
int lang_bytecode_table_append_undefined(lang_bytecode_table_t* table, const lang_bytecode_operand_t* dest);

/**
 * @brief get the opcode from the bytecode table
 * @param table the target bytecode table
 * @param offset the bytecode offset
 * @return the opcode or error code
 **/
int lang_bytecode_table_get_opcode(const lang_bytecode_table_t* table, uint32_t offset);

/**
 * @brief get the number of operands from the bytecode table
 * @param table the target bytecode table
 * @param offset the bytecode offset
 * @return the number of operands of the instruction or error code
 **/
uint32_t lang_bytecode_table_get_num_operand(const lang_bytecode_table_t* table, uint32_t offset);

/**
 * @brief aquire a string id in the bytecode table for the given string
 * @note this function means if there's no such string, the function will add a new entry in the string table
 * @param table the target bytecode table
 * @param str the string to query
 * @return the string id, or error code
 **/
uint32_t lang_bytecode_table_acquire_string_id(lang_bytecode_table_t* table, const char* str);

/**
 * @brief get the operand
 * @param table the target bytecode table
 * @param offset the bytecode offest
 * @param n the n-th operand of this instruction
 * @param result the result buffer
 * @return status code
 **/
int lang_bytecode_table_get_operand(const lang_bytecode_table_t* table, uint32_t offset, uint32_t n, lang_bytecode_operand_id_t* result);

/**
 * @brief convert a bytecode operand ID to string
 * @param table the target table
 * @param id the operand id
 * @return result string or NULL on error
 **/
const char* lang_bytecode_table_str_id_to_string(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id);

/**
 * @brief get the number of section of this symbol
 * @param table the target bytecode table
 * @param id the operand id
 * @return the number of sections or error code
 **/
uint32_t lang_bytecode_table_sym_id_length(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id);

/**
 * @brief read a smybol reference and convert the n-th section to string
 * @param table the target table
 * @param id the operand id
 * @param n the n-th section
 * @return result string or NULL on error
 **/
const char* lang_bytecode_table_sym_id_to_string(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id, uint32_t n);

/**
 * @brief output the human readable form of byte code to the string buffer
 * @param table the source bytecode table
 * @param offset the offest of the instruction
 * @param buffer the target string buffer
 * @return status code
 **/
int lang_bytecode_table_append_to_string_buffer(const lang_bytecode_table_t* table, uint32_t offset, string_buffer_t* buffer);

/**
 * @brief get the number of bytecodes in the target bytecode table
 * @param table the bytecode table
 * @return the number of bytecode table or error code
 **/
uint32_t lang_bytecode_table_get_num_bytecode(const lang_bytecode_table_t* table);

/**
 * @brief print the content of bytecodes table to the log
 * @param table the target bytecode table
 * @return status code
 **/
int lang_bytecode_table_print(const lang_bytecode_table_t* table);

/**
 * @brief get the string id from the bytecode table.
 * @note this is similar to the function lang_bytecode_table_acquire_string_id, the only difference of this function is
 *       it do not insert new item in to the string table if the string is not found
 * @param table the bytecode table
 * @param str the target string or error code
 **/
uint32_t lang_bytecode_table_get_string_id(const lang_bytecode_table_t* table, const char* str);

/**
 * @brief get an array of the string id based on the symbol id. This is used when we
 *        match the property, becuase all the property is stored in symbol id, so this function
 *        is the fastest way to match a symbol
 * @param table the bytecode table
 * @param id the operand id
 * @return the result, NULL on error
 **/
const uint32_t* lang_bytecode_table_sym_id_to_strid_array(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id);

/**
 * @brief add a new symbol to the bytecode table
 * @note this function is only used for testing purpose
 * @param table the bytecode table
 * @param str the string representation of this symbol
 * @return the symbol id or error code
 **/
uint32_t lang_bytecode_table_insert_symbol(lang_bytecode_table_t* table, const char* str);

/**
 * @brief create a new bytecode label table
 * @return the newly created bytecode label table or NULL
 **/
lang_bytecode_label_table_t* lang_bytecode_label_table_new();

/**
 * @brief dispose a used label table
 * @param table the label table to dispose
 * @return status code
 **/
int lang_bytecode_label_table_free(lang_bytecode_label_table_t* table);

/**
 * @brief get a new label id
 * @param table the label table
 * @return the next label id or error code
 **/
uint32_t lang_bytecode_label_table_new_label(lang_bytecode_label_table_t* table);

/**
 * @brief assign the label to current bytecode table location
 * @param label_table the label table
 * @param bc_table the bytecode table
 * @param id the label id
 * @return status code
 **/
int lang_bytecode_label_table_assign_current_address(lang_bytecode_label_table_t* label_table, const lang_bytecode_table_t* bc_table, uint32_t id);

/**
 * @brief patch the bytecode table with the label table, which makes all the labels substituded with the address in the bytecode table
 * @param table the bytecdoe table
 * @param labels the label table
 * @return status code
 **/
int lang_bytecode_table_patch(lang_bytecode_table_t* table, const lang_bytecode_label_table_t* labels);

#endif /* __PLUMBER_LANG_BYTECODE_H__ */
