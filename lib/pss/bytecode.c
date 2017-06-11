/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <pss/bytecode.h>

/**
 * @brief Represents a valid opcode
 **/
typedef struct {
	pss_bytecode_info_t   info;   /*!< The information of the instruction */
	const char*           name;   /*!< The name of the instruction */
} _bytecode_desc_t;

#define _BYTECODE(_name, op, _rtype, _has_const, nreg) [PSS_BYTECODE_OPCODE_##_name] {\
	.info = {\
		.operation = PSS_BYTECODE_OP_##op,\
		.rtype     = PSS_BYTECODE_RTYPE_##_rtype,\
		.has_const = _has_const,\
		.num_regs  = nreg\
	},\
	.name = #_name\
}

static const _bytecode_desc_t _bytecode[] = {
   /*         name           operation  rtype    const?  nreg    */
	_BYTECODE(DICT_NEW     , NEW      , DICT   ,     0,    1),   /* dict-new R0*/
	_BYTECODE(CLOSURE_NEW  , NEW      , CLOSURE,     0,    2),   /* closure-new R0, R1*/
	_BYTECODE(INT_LOAD     , LOAD     , INT    ,     1,    1),   /* int-load(3) R0 */
	_BYTECODE(STR_LOAD     , LOAD     , STR    ,     1,    1),   /* str-load(3) R0 */
	_BYTECODE(LENGTH       , LEN      , GENERIC,     0,    2),   /* length R0, R1: R1 = R0.length */
	_BYTECODE(GET_VAL      , GETVAL   , GENERIC,     0,    3),   /* get R0, R1, R2: R2 = R0[R1] */
	_BYTECODE(SET_VAL      , SETVAL   , GENERIC,     0,    3),   /* set R0, R1, R2: R1[R2] = R0 */
	_BYTECODE(GET_KEY      , GETKEY   , GENERIC,     0,    3),   /* key R0, R1, R2: R2 = R1-th key in R0 */
	_BYTECODE(CALL         , CALL     , GENERIC,     0,    3),   /* call R0, R1, R2: R2 = R0(*R1) */
	_BYTECODE(BUILTIN      , BUILTIN  , GENERIC,     0,    3),   /* builtin R0, R1, R2: R2 = Invoke builtin named R0 with param R1 */
	_BYTECODE(JUMP         , JUMP     , GENERIC,     0,    1),   /* jump R0 = jump to address R0 */
	_BYTECODE(JZ           , JZ       , GENERIC,     0,    2),   /* jz R0, R1 = jump to address R1 when R0 == 0 */
	_BYTECODE(ADD          , ADD      , GENERIC,     0,    3),   /* add R0, R1, R2 = R2 = R0 + R1 */
	_BYTECODE(SUB          , SUB      , GENERIC,     0,    3),   /* sub R0, R1, R2 = R2 = R0 - R1 */
	_BYTECODE(MUL          , MUL      , GENERIC,     0,    3),   /* mul R0, R1, R2 = R2 = R0 * R1 */
	_BYTECODE(DIV          , DIV      , GENERIC,     0,    3),   /* div R0, R1, R2 = R2 = R0 / R1 */
	_BYTECODE(LT           , LT       , GENERIC,     0,    3),   /* less-than R0, R1, R2 = R2 = (R0 < R1) */
	_BYTECODE(EQ           , EQ       , GENERIC,     0,    3),   /* less-than R0, R1, R2 = R2 = (R0 < R1) */
	_BYTECODE(AND          , AND      , GENERIC,     0,    3),   /* and R0, R1, R2 = R2 = R0 and R1 */
	_BYTECODE(OR           , OR       , GENERIC,     0,    3),   /* or R0, R1, R2 = R2 = R0 or R1 */
	_BYTECODE(XOR          , XOR      , GENERIC,     0,    3),   /* xor R0, R1, R2 = R2 = R0 xor R1 */
	_BYTECODE(MOVE         , MOVE     , GENERIC,     0,    2),   /* move R0, R1 = R0 = R1 */
	_BYTECODE(GLOBAL_GET   , GLOBALGET, GENERIC,     0,    2),
	_BYTECODE(GLOBAL_SET   , GLOBALSET, GENERIC,     0,    2) 
};
/**
 * We should make sure we have everything we need in the list
 * @note Once you add a new instruction to the instruction set but haven't update the bytecode table,
 *       You should get a compile error at this point. To resolve this, you must add the newly added 
 *       instruction to the list
 **/
STATIC_ASSERTION_EQ_ID(__filled_all_bytecode__, sizeof(_bytecode) / sizeof(_bytecode[0]), PSS_BYTECODE_OPCODE_COUNT);

#undef _BYTECODE

/**
 * @brief The data structure for an internal table
 **/
typedef struct {
	size_t    capacity;   /*!< The capacity of the string table */
	size_t    size;       /*!< The actual size of the string table */
	uintptr_t __padding__[0];
	union {
		char*                 string[0];     /*!< The string array */
		pss_bytecode_regid_t  regid[0];      /*!< The register ID array */
		pss_bytecode_inst_t   inst[0];       /*!< The instruction array */
	};
} _table_t;
STATIC_ASSERTION_LAST(_table_t, string);
STATIC_ASSERTION_SIZE(_table_t, string, 0);

/**
 * @brief The actual structure for the segment
 **/
struct _pss_bytecode_segment_t {
	_table_t*         string_table;   /*!< The string constant table for this code segment */
	_table_t*         argument_table; /*!< The argument table, which indicates what register needs to be initialied by the callee */
	_table_t*         code_table;     /*!< The table of the function body */
};

/**
 * @brief The internal data structure for a bytecode table
 **/
struct _pss_byteode_table_t {
	uint32_t count;                  /*!< The number of the bytecode segments in the bytecode table */
	uintptr_t __padding__[0];
	pss_bytecode_segment_t segs[0];  /*!< The code segment array */
};


