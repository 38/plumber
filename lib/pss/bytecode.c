/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <error.h>

#include <pss/log.h>
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
 * @brief The data structure for internal data tables
 **/
typedef struct {
	uint32_t    capacity;   /*!< The capacity of the string table */
	uint32_t    size;       /*!< The actual size of the string table */
	enum {
		_TABLE_TYPE_STR,  /*!< This is a string table */
		_TABLE_TYPE_REG,  /*!< This is a register table */
		_TABLE_TYPE_INST  /*!< This is an instruction table */
	} type;               /*!< What kind of table it is */
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
struct _pss_bytecode_module_t {
	uint32_t count;                  /*!< The number of the bytecode segments in the bytecode table */
	uintptr_t __padding__[0];
	pss_bytecode_segment_t segs[0];  /*!< The code segment array */
};
STATIC_ASSERTION_SIZE(pss_bytecode_module_t, segs, 0);
STATIC_ASSERTION_LAST(pss_bytecode_module_t, segs);

/**
 * @brief The magic number used to identify the PSS bytecode file header
 **/
const uint64_t _file_header = 0x76737065ffffffull;

/**
 * @brief Dump a instruction to the output file
 * @param inst The instruction to dump
 * @param out The output file pointer
 * @return status code
 **/
static inline int _dump_inst(const pss_bytecode_inst_t* inst, FILE* out)
{
	if(inst->opcode >= PSS_BYTECODE_OPCODE_COUNT)
		ERROR_RETURN_LOG(int, "Invalid instruction opcode = %x", inst->opcode);
	const pss_bytecode_info_t* info = &_bytecode[inst->opcode].info;

	if(1 != fwrite(&inst->opcode, sizeof(inst->opcode), 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the opcode to the output file");
	if(info->has_const && 1 != fwrite(&inst->num, sizeof(inst->num), 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the const number to the output file");

	if(info->num_regs != fwrite(inst->reg, sizeof(pss_bytecode_regid_t) * info->num_regs, 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the register list to the file");

	return 0;
}

/**
 * @brief Dump a string to the output file
 * @param str The string to dump
 * @param out The output file
 * @return status code
 **/
static inline int _dump_string(const char* str, FILE* out)
{
	uint32_t size = (uint32_t)strlen(str);

	if(1 != fwrite(&size, sizeof(size), 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump size of string to the output file");

	if(1 != fwrite(str, size, 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the string content to output file");

	return 0;
}

/**
 * @brief Dump a table to the bytecode file
 * @param table The bytecode table to dump
 * @param out The output file pointer
 * @return status code
 **/
static inline int _dump_table(const _table_t* table, FILE* out)
{
	if(fwrite(&table->size, sizeof(table->size), 1, out) != 1)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the size of the data table to output file");

	uint32_t i;
	for(i = 0; i < table->size; i ++)
	{
		switch(table->type)
		{
			case _TABLE_TYPE_STR:
				if(ERROR_CODE(int) == _dump_string(table->string[i], out))
					ERROR_RETURN_LOG(int, "Cannot dump string to file");
				break;
			case _TABLE_TYPE_REG:
				if(1 == fwrite(table->regid + i, sizeof(table->regid[i]), 1, out))
					ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the register id to the output file");
				break;
			case _TABLE_TYPE_INST:
				if(ERROR_CODE(int) == _dump_inst(table->inst + i, out))
					ERROR_RETURN_LOG_ERRNO(int, "Cannot dump instruction to the output file");
		}
	}

	return 0;
}

/**
 * @brief Dump an bytecode segment to the bytecode file
 * @param seg The segment to dump
 * @param out The output file pointer
 * @return status code
 **/
static inline int _dump_segment(const pss_bytecode_segment_t* seg, FILE* out)
{
	if(ERROR_CODE(int) == _dump_table(seg->string_table, out))
		ERROR_RETURN_LOG(int, "Cannot dump the string table to output file");

	if(ERROR_CODE(int) == _dump_table(seg->argument_table, out))
		ERROR_RETURN_LOG(int, "Cannot dump the argument table to the output file");

	if(ERROR_CODE(int) == _dump_table(seg->code_table, out))
		ERROR_RETURN_LOG(int, "Cannot dump the code table to the output file");

	return 0;
}

/**
 * @brief Dump the entire bytecode table to the file
 * @param table The bytecode table to dump
 * @param out   The out put file pointer
 * @return status code
 **/
static inline int _dump_module(const pss_bytecode_module_t* module, FILE* out)
{
	if(1 != fwrite(module, sizeof(*module), 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the table header to the file");

	uint32_t i;
	for(i = 0; i < module->count; i ++)
		if(ERROR_CODE(int) == _dump_segment(module->segs + i, out))
			ERROR_RETURN_LOG(int, "Cannot dump segment to the bytecode file");

	return 0;
}

int pss_bytecode_table_dump(const pss_bytecode_module_t* module, const char* path)
{
	if(NULL == module || NULL == path) ERROR_RETURN_LOG(int, "Invalid arguments");
	FILE* fp = fopen(path, "wb");
	if(NULL == fp) ERROR_RETURN_LOG_ERRNO(int, "Cannot open file %s for write", path);

	if(1 != fwrite(&_file_header, sizeof(_file_header), 1, fp))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot write the file header");

	if(ERROR_CODE(int) == _dump_module(module, fp))
		ERROR_LOG_GOTO(ERR, "Cannot dump the bytecode table content");

	fclose(fp);
	return 0;
ERR:
	if(NULL != fp)
	{
		fclose(fp);
		unlink(path);
	}

	return ERROR_CODE(int);
}
