/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The bytecode definition of the Plumber Service Script VM
 * @file  pss/bytecode.h
 **/

#include <utils/static_assertion.h>

#ifndef __PSS_BYTECODE_H__
#define __PSS_BYTECODE_H__

/**
 * @brief The identifier of a segment in a bytecode table
 **/
typedef uint32_t pss_bytecode_segid_t;

/**
 * @brief The address which is used to addressing one instruction inside a code segment
 **/
typedef uint32_t pss_bytecode_addr_t;

/**
 * @brief The in-segment label id
 **/
typedef uint32_t pss_bytecode_label_t;

/**
 * @brief The code used to identify what kinds of operation it is 
 **/
typedef enum {
	PSS_BYTECODE_OP_NEW,    /*!< Create a new value */
	PSS_BYTECODE_OP_LOAD,   /*!< Load new value from the constant table */
	PSS_BYTECODE_OP_LEN,    /*!< Get the length of the object */
	PSS_BYTECODE_OP_GETVAL, /*!< Get a field of the object */
	PSS_BYTECODE_OP_SETVAL, /*!< Set a field of the object */
	PSS_BYTECODE_OP_GETKEY, /*!< Get the n-th key in the object */
	PSS_BYTECODE_OP_CALL,   /*!< Call an callable object */
	PSS_BYTECODE_OP_BUILTIN,/*!< Call a built-in function */
	PSS_BYTECODE_OP_JUMP,   /*!< Jump */
	PSS_BYTECODE_OP_JZ,     /*!< Jump on zero */
	PSS_BYTECODE_OP_ADD,    /*!< Addition */
	PSS_BYTECODE_OP_SUB,    /*!< Substraction */
	PSS_BYTECODE_OP_MUL,    /*!< Mulplication */
	PSS_BYTECODE_OP_DIV,    /*!< Division */
	PSS_BYTECODE_OP_LT,     /*!< Less than */
	PSS_BYTECODE_OP_EQ,     /*!< Equal to */
	PSS_BYTECODE_OP_AND,    /*!< Boolean and */
	PSS_BYTECODE_OP_OR,     /*!< Boolean or */
	PSS_BYTECODE_OP_XOR,    /*!< Boolean xor */
	PSS_BYTECODE_OP_MOVE,   /*!< Data transfer */
	PSS_BYTECODE_OP_GLOBALGET,  /*!< Get the global variable */
	PSS_BYTECODE_OP_GLOBALSET,  /*!< Set the gloabl variable */
	/* DO NOT INSERT ITEMS BELOW */
	PSS_BYTECODE_OP_COUNT
} pss_bytecode_op_t;
STATIC_ASSERTION_LT(PSS_BYTECODE_OP_COUNT, 256);

/**
 * @brief The code used to identify what is the return type of the instruction
 * @note  This field is used to distinguish the new insturction for different types
 **/
typedef enum {
	PSS_BYTECODE_RTYPE_GENERIC, /*!< A generic opcode */
	PSS_BYTECODE_RTYPE_INT,     /*!< The instruction is about interger operation */
	PSS_BYTECODE_RTYPE_DICT,    /*!< Dict operations */
	PSS_BYTECODE_RTYPE_STR,     /*!< String operations */
	PSS_BYTECODE_RTYPE_CLOSURE, /*!< Closure operations */
	PSS_BYTECODE_RTYPE_UNDEF,   /*!< Undefiend operations */
	/* DO NOT INSERT ITEMS BELOW */
	PSS_BYTECODE_RTYPE_COUNT
} pss_bytecode_rtype_t;
STATIC_ASSERTION_LT(PSS_BYTECODE_RTYPE_COUNT, 256);

/**
 * @brief The actual operation code used in the instruction 
 * @details 
 *     |  Instruction  |             Example                   |                Behavior               |
 *     |:------------  |:-----------------------------------   |---------------------------------------|
 *     |  DICT_NEW     | **DICT_NEW** *R_out*                  | *R_taget* = new Dictionary() |
 *     |  CLOSURE_NEW  | **CLOSURE_NEW** *R_func*, *R_out*     | Make a closure combines current stack frame with the code carried by *R_func* and store it in out   | 
 *     |  INT_LOAD     | **INT_LOAD(n)** *R_out*               | *R_out*    = n |
 *     |  STR_LOAD     | **STR_LOAD(n)** *R_out*               | *R_out*    = string_table(n) |
 *     |  LENGTH       | **LENGTH** *R_in*, *R_out*            | *R_out*    = *R_in*.length, only valid when *R_in* is string, dict and code |
 *     |  GET_VAL      | **GET_VAL** *R_obj*, *R_key*, *R_out* | *R_out*   = *R_obj*[*R_key*], only valid when *R_obj* is a string or dict and *R_key* will be converted to string anyway |
 *     |  SET_VAL      | **SET_VAL** *R_in*, *R_obj*, *R_key*  | *R_obj*[*R_key*] = *R_out*, only valid when *R_obj* is a string or dict and *R_key* will be converted to string anyway |
 *     |  GET_KEY      | **GET_KEY** *R_obj*, *R_idx*, *R_out* | *R_out*    = the *R_idx*-th key in the *R_obj*, only valid when *R_obj* is a dict |
 *     |  CALL         | **CALL** *R_func*, *R_arg*, *R_out*   | *R_out*    = *R_func*(*R_arg*) |
 *     |  BUILTIN      | **BUILTIN** *R_name*, *R_arg*, *R_out*| *R_out*    = &lt;builtin function named *R_name*&gt;(*R_arg*) |
 *     |  JUMP         | **JUMP** *R_address*                  | Jump to *R_address* |
 *     |  JZ           | **JZ** *R_cond*, *R_address*          | if(*R_cond* == 0) jump *R_address*; |
 *     |  ADD          | **ADD** *R_1*, *R_2*, *R_out*         | *R_out*    = *R_1* + *R_2* (Numeric plus or string concatenate or list concatenate) |
 *     |  SUB          | **SUB** *R_1*, *R_2*, *R_out*         | *R_out*    = *R_1* - *R_2* (Only valid when *R_1* and *R_2* are int) |
 *     |  MUL          | **MUL** *R_1*, *R_2*, *R_out*         | *R_out*    = *R_1* * *R_2* (Only valid when *R_1* and *R_2* are int) |
 *     |  DIV          | **DIV** *R_1*, *R_2*, *R_out*         | *R_out*    = *R_1* / *R_2* (Only valid when *R_1* and *R_2* are int) |
 *     |  LT           | **LT** *R_1*, *R_2*, *R_out*          | *R_out*    = *R_1* &lt; *R_2* (Numeric comparasion, List comparasion or string ) |
 *     |  EQ           | **EQ** *R_1*, *R_2*, *R_out*          | *R_out*    = *R_1* == *R_2* |
 *     |  AND          | **AND** *R_1*, *R_2*, *R_out*         | *R_out*    = *R_1* && *R_2* |
 *     |  OR           | **OR** *R_1*,  *R_2*, *R_out*         | *R_out*    = *R_1* or *R_2* |
 *     |  XOR          | **XOR** *R_1*, *R_2*, *R_out*         | *R_out*    = *R_1* ^ *R_2* |
 *     |  MOVE         | **MOVE** *R_in*, *R_out*              | *R_out*    = *R_in* |
 *     |  GLOBAL_GET   | **GLOBAL_GET** *R_name*, *R_out*      | *R_out*    = global[*R_name*]|
 *     |  GLOBAL_SET   | **GLOBAL_SET** *R_in*, *R_name*       | global[*R_name*] = *R_in* |
 **/
typedef enum {
	PSS_BYTECODE_OPCODE_DICT_NEW,
	PSS_BYTECODE_OPCODE_CLOSURE_NEW,
	PSS_BYTECODE_OPCODE_INT_LOAD,
	PSS_BYTECODE_OPCODE_STR_LOAD,
	PSS_BYTECODE_OPCODE_LENGTH,
	PSS_BYTECODE_OPCODE_GET_VAL,
	PSS_BYTECODE_OPCODE_SET_VAL,
	PSS_BYTECODE_OPCODE_GET_KEY,
	PSS_BYTECODE_OPCODE_CALL,
	PSS_BYTECODE_OPCODE_BUILTIN,
	PSS_BYTECODE_OPCODE_JUMP,
	PSS_BYTECODE_OPCODE_JZ,
	PSS_BYTECODE_OPCODE_ADD,
	PSS_BYTECODE_OPCODE_SUB,
	PSS_BYTECODE_OPCODE_MUL,
	PSS_BYTECODE_OPCODE_DIV,
	PSS_BYTECODE_OPCODE_LT,
	PSS_BYTECODE_OPCODE_EQ,
	PSS_BYTECODE_OPCODE_AND,
	PSS_BYTECODE_OPCODE_OR,
	PSS_BYTECODE_OPCODE_XOR,
	PSS_BYTECODE_OPCODE_MOVE,
	PSS_BYTECODE_OPCODE_GLOBAL_GET,
	PSS_BYTECODE_OPCODE_GLOBAL_SET,
	/* DO NOT INSERT ITEMS BELOW */
	PSS_BYTECODE_OPCODE_COUNT
} pss_bytecode_opcode_t;

/**
 * @brief The information about the bytecode operation
 **/
typedef struct {
	pss_bytecode_op_t      operation:8;                          /*!< The operation code */
	pss_bytecode_rtype_t   rtype:8;                              /*!< The return type code */
	uint8_t                has_const:1;                          /*!< If the instruction contains a const */
	uint8_t                num_regs:7;                            /*!< How many registers */
} pss_bytecode_info_t;

/**
 * @brief An actual instruction
 **/
typedef struct {
	pss_bytecode_opcode_t opcode;   /*!< The opcode */
	int                   num;      /*!< The numeric literal list */
	uint16_t*             reg;      /*!< The register reference */
} pss_bytecode_inst_t;

/**
 * @brief Represent a bytecode segment, every function is a bytecode segment 
 * @note Segment contains a bytecode series, an param + closure local regiester map + string table 
 **/
typedef struct _pss_bytecode_segment_t pss_bytecode_segment_t;

/**
 * @brief represents a bytecode table, each source code file will be compiled to a source table
 * @note  The bytecode table contains multiple bytecode segments, and each segments as an id
 **/
typedef struct _pss_byteode_table_t pss_bytecode_table_t;

/**
 * @brief Create a empty bytecode table
 * @return the newly created bytecode table, NULL on error case
 **/
pss_bytecode_table_t* pss_bytecode_table_new();

/**
 * @brief Dispose a used bytecode table
 * @param table The bytecode table to dispose
 * @return status code
 **/
int pss_bytecode_table_free(pss_bytecode_table_t* table);

/**
 * @brief Append a segment to the bytecode table
 * @param table the bytecode table
 * @param segment the segment to add
 * @return The segment id, or error code
 **/
pss_bytecode_segid_t pss_bytecode_table_append(pss_bytecode_table_t* table, const pss_bytecode_segment_t* segment);

/**
 * @brief Get the segment from the id
 * @param table The table we want to get
 * @param id The id we want to query
 * @return the segment or NULL on error
 **/
const pss_bytecode_segment_t* pss_bytecode_table_get(const pss_bytecode_table_t* table, pss_bytecode_segid_t id);

/**
 * @brief Get the segment id of the entry point of this bytecode table
 * @param table The bytecode table 
 * @return The segment ID for the entry point or error code
 **/
pss_bytecode_segid_t pss_bytecode_table_get_entry_point(const pss_bytecode_table_t* table);

/**
 * @brief Set the entry point segment id of the given bytecode table
 * @param table The bytecode table to set
 * @param id the entry point segment id
 * @return status code
 **/
int pss_bytecode_table_set_entry_point(pss_bytecode_table_t* table, pss_bytecode_segid_t  id);

/**
 * @brief Create a new bytecode segment
 * @return The newly created bytecode segment
 **/
pss_bytecode_segment_t* pss_bytecode_segment_new();

/**
 * @brief Dispose a used bytecode table
 * @param segment the used bytecode table
 * @return status code
 **/
int pss_bytecode_segment_free(pss_bytecode_segment_t* segment);

/**
 * @brief Allocate a new label in the bytecode table
 * @note  This function is used in code generation, because sometimes, we knows we should
 *        have a jump instruction at this point, but the target address is still unknown.
 *        So this makes us need to use a placeholder for this address and patch it later
 * @param segment The code segment we want to define the label in
 * @return The label id or error code
 **/
pss_bytecode_label_t pss_bytecode_segment_label_alloc(pss_bytecode_segment_t* segment);

/**
 * @brief Patch the label with the given address
 * @note This function is a part of the label mechanism, when the code generator do not
 *       know the jump target, it allocates a label and move on. Whenever the code gnereator
 *       has generated the target instruction, which means the address is known for now, we
 *       use this function to patch the bytecode
 * @param segment The code segment we want to patch
 * @param label   The label we want to patch
 * @param addr    The address value we want to patch
 * @return status code
 **/
int pss_bytecode_segment_patch_label(pss_bytecode_segment_t* segment, pss_bytecode_label_t label, pss_bytecode_addr_t addr);

/**
 * @brief Append a new instruction to the segment
 * @param segment The segment we want append the instruction to
 * @param opcode The opcode we want to append
 * @param num    The numeric constant for this instruction, if the instruction do not requires an constant, it will be ignored
 * @param str    The string constant for this instruction, if the instruction do not requires an string const, it will be ignored
 * @param label  The label we want to use as an numeric constant placeholder 
 * @note  the varargs is the register list, end with ERROR_CODE(int)
 * @return The address of the newly appended instruction
 **/
pss_bytecode_addr_t  pss_bytecode_segment_append_inst(pss_bytecode_segment_t* segment, 
		                                              pss_bytecode_opcode_t opcode, 
		                                              int num, pss_bytecode_label_t label, const char* str, 
													  ...) __attribute__((sentinel));

/**
 * @brief Get the instruction information header at given address in the bytecode segment
 * @param segment the segment we want to access
 * @param addr The address 
 * @param infobuf The buffer used to return the instruction info
 * @param numbuf The buffer used to return the numeric consetant
 * @param strbuf The buffer used to return the string constant
 * @param regbuf The buffer used to return the register list
 * @return status code
 **/
int pss_bytecode_segment_get_inst(const pss_bytecode_segment_t* segment, pss_bytecode_addr_t addr, 
		                          pss_bytecode_info_t* infobuf, int* numbuf, char const** strbuf, int const ** regbuf);

/**
 * @brief Get the human-readable representation of the instruction at the address addr in the segment
 * @param segment the segment we want to peek
 * @param addr    The address
 * @param buf     The string buffer
 * @param sz      The string buffer size
 * @return        The result string representation of the instruction
 **/
const char* pss_bytecode_segment_inst_str(const pss_bytecode_segment_t* segment, pss_bytecode_addr_t addr, char* buf, size_t sz);

#endif /* __PSS_BYTECODE_H__ */