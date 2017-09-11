/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <zlib.h>

#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>

/**
 * @brief An internal represetation of a instruction
 **/
typedef struct {
	pss_bytecode_opcode_t opcode;    /*!< The opcode */
	pss_bytecode_numeric_t num;      /*!< The numeric literal if applies*/
	pss_bytecode_label_t  label;     /*!< The label if applies (This is used for code generation only, and should be ignored otherwise) */
	pss_bytecode_regid_t  reg[4];    /*!< The register reference */
} _inst_t;

/**
 * @brief Represents a valid opcode
 **/
typedef struct {
	pss_bytecode_opcode_t opcode; /*!< The opcode of this instruction */
	pss_bytecode_info_t   info;   /*!< The information of the instruction */
} _bytecode_desc_t;

#define _BYTECODE(_name, op, _rtype, _has_const, _str_ref, nreg) {\
	.opcode = PSS_BYTECODE_OPCODE_##_name, \
	.info = {\
		.name = #_name,\
		.operation = PSS_BYTECODE_OP_##op,\
		.rtype     = PSS_BYTECODE_RTYPE_##_rtype,\
		.has_const = _has_const,\
		.string_ref = _str_ref,\
		.num_regs  = nreg\
	}\
}

/**
 * @brief The bytecode information table, which maps opcode to the opcode information
 * @note  This is actually a constant table. So the most straightforward way to do this
 *        is use a constant array with desginated initializers.
 *        However, this makes our compile time check for "all the instuctions are registered
 *        at this point" have no effect. Because the designated initializers will fill the
 *        holes with 0.
 *        So this makes us do not use the designated initializers, instead, we initialize
 *        the array randomly, but we do the check on compile time. And when the code runs
 *        we sort the array at first time it has been used
 **/
static _bytecode_desc_t _bytecode[] = {
	/*         name           operation  rtype    const?   strref nreg       behavior*/
	_BYTECODE(DICT_NEW     , NEW      , DICT   ,     0,     0,    1),   /* dict-new R0*/
	_BYTECODE(CLOSURE_NEW  , NEW      , CLOSURE,     0,     0,    2),   /* closure-new R0, R1*/
	_BYTECODE(INT_LOAD     , LOAD     , INT    ,     1,     0,    1),   /* int-load(3) R0 */
	_BYTECODE(STR_LOAD     , LOAD     , STR    ,     1,     1,    1),   /* str-load(3) R0 */
	_BYTECODE(LENGTH       , LEN      , GENERIC,     0,     0,    2),   /* length R0, R1: R1 = R0.length */
	_BYTECODE(GET_VAL      , GETVAL   , GENERIC,     0,     0,    3),   /* get R0, R1, R2: R2 = R0[R1] */
	_BYTECODE(SET_VAL      , SETVAL   , GENERIC,     0,     0,    3),   /* set R0, R1, R2: R1[R2] = R0 */
	_BYTECODE(GET_KEY      , GETKEY   , GENERIC,     0,     0,    3),   /* key R0, R1, R2: R2 = R1-th key in R0 */
	_BYTECODE(ARG          , ARG      , GENERIC,     0,     0,    1),   /* arg Rx */
	_BYTECODE(CALL         , CALL     , GENERIC,     0,     0,    2),   /* call R0, R1: R1 = R0(*R1) */
	_BYTECODE(RETURN       , RETURN   , GENERIC,     0,     0,    1),   /* return R0 */
	_BYTECODE(JUMP         , JUMP     , GENERIC,     0,     0,    1),   /* jump R0 = jump to address R0 */
	_BYTECODE(JZ           , JZ       , GENERIC,     0,     0,    2),   /* jz R0, R1 = jump to address R1 when R0 == 0 */
	_BYTECODE(ADD          , ADD      , GENERIC,     0,     0,    3),   /* add R0, R1, R2 = R2 = R0 + R1 */
	_BYTECODE(SUB          , SUB      , GENERIC,     0,     0,    3),   /* sub R0, R1, R2 = R2 = R0 - R1 */
	_BYTECODE(MUL          , MUL      , GENERIC,     0,     0,    3),   /* mul R0, R1, R2 = R2 = R0 * R1 */
	_BYTECODE(DIV          , DIV      , GENERIC,     0,     0,    3),   /* div R0, R1, R2 = R2 = R0 / R1 */
	_BYTECODE(MOD          , MOD      , GENERIC,     0,     0,    3),   /* div R0, R1, R2 = R2 = R0 / R1 */
	_BYTECODE(LT           , LT       , GENERIC,     0,     0,    3),   /* less-than R0, R1, R2 = R2 = (R0 < R1) */
	_BYTECODE(LE           , LE       , GENERIC,     0,     0,    3),   /* less-equal R0, R1, R2 = R2 = (R0 <= R1) */
	_BYTECODE(GT           , GT       , GENERIC,     0,     0,    3),   /* greater-than R0, R1, R2 = R2 = (R0 > R1) */
	_BYTECODE(GE           , GE       , GENERIC,     0,     0,    3),   /* greater-equal R0, R1, R2 = R2 = (R0 >= R1) */
	_BYTECODE(EQ           , EQ       , GENERIC,     0,     0,    3),   /* equal R0, R1, R2 = R2 = (R0 == R1) */
	_BYTECODE(NE           , NE       , GENERIC,     0,     0,    3),   /* not-equal R0, R1, R2 = R2 = (R0 != R1) */
	_BYTECODE(AND          , AND      , GENERIC,     0,     0,    3),   /* and R0, R1, R2 = R2 = R0 and R1 */
	_BYTECODE(OR           , OR       , GENERIC,     0,     0,    3),   /* or R0, R1, R2 = R2 = R0 or R1 */
	_BYTECODE(XOR          , XOR      , GENERIC,     0,     0,    3),   /* xor R0, R1, R2 = R2 = R0 xor R1 */
	_BYTECODE(MOVE         , MOVE     , GENERIC,     0,     0,    2),   /* move R0, R1 = R1 = R0 */
	_BYTECODE(GLOBAL_GET   , GLOBALGET, GENERIC,     0,     0,    2),   /* global R0, R1 = R1 = global(R0) */
	_BYTECODE(GLOBAL_SET   , GLOBALSET, GENERIC,     0,     0,    2),   /* global R0, R1 = global(R1) = R0 */
	_BYTECODE(UNDEF_LOAD   , LOAD     , UNDEF  ,     0,     0,    1),   /* undef-load R0 = R0 = undefined */
	_BYTECODE(DINFO_LINE   , DEBUGINFO, INT    ,     1,     0,    0),   /* dbginf-line(10) */
	_BYTECODE(DINFO_FUNC   , DEBUGINFO, STR    ,     1,     1,    0)    /* dbginf-func(test) */
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
 * @brief The type of the table
 **/
typedef enum {
	_TABLE_TYPE_STR,   /*!< Represents a string table */
	_TABLE_TYPE_REG,   /*!< Represents a register table */
	_TABLE_TYPE_INST   /*!< Represents an instruction table */
} _table_type_t;

typedef  struct {
	_table_type_t type;       /*!< What kind of table it is */
	uint32_t      size;       /*!< The actual size of the string table */
} __attribute__((packed)) _table_header_t;    /*!< The header of the table */

/**
 * @brief The data structure for internal data tables
 **/
typedef struct {
	_table_header_t header;   /*!< The header of the table */
	uint32_t      capacity;   /*!< The capacity of the string table */
	uintptr_t __padding__[0];
	union {
		char*                 string[0];     /*!< The string array */
		pss_bytecode_regid_t  regid[0];      /*!< The register ID array */
		_inst_t               inst[0];       /*!< The instruction array */
	};
} _table_t;
/* Although if we do assertion on string only it will check everything blow
 * But this is based on all the data section are put in a union. And if we
 * do this, we explicitly describes the constain */
STATIC_ASSERTION_LAST(_table_t, string);
STATIC_ASSERTION_SIZE(_table_t, string, 0);
STATIC_ASSERTION_LAST(_table_t, regid);
STATIC_ASSERTION_SIZE(_table_t, regid, 0);
STATIC_ASSERTION_LAST(_table_t, inst);
STATIC_ASSERTION_SIZE(_table_t, inst, 0);

/**
 * @brief The actual structure for the segment
 **/
struct _pss_bytecode_segment_t {
	pss_bytecode_label_t  next_label;     /*!< The next unused label */
	_table_t*             argument_table; /*!< The argument table, which indicates what register needs to be initialied by the callee */
	_table_t*             string_table;   /*!< The string constant table for this code segment */
	_table_t*             code_table;     /*!< The table of the function body */
};

/**
 * @brief The module file header
 **/
typedef struct {
	uint64_t magic_num;           /*!< The magic number */
	uint32_t nseg;                /*!< How many segments in the module */
	pss_bytecode_segid_t entry_point;  /*!< The entry point segment */
} __attribute__((packed)) _module_header_t;

/**
 * @brief The internal data structure for a bytecode table
 **/
struct _pss_bytecode_module_t {
	_module_header_t header;                /*!< The number of the bytecode segments in the bytecode table */
	uint32_t         capacity;              /*!< The capacity of the bytecode table */
	pss_bytecode_segment_t** segs;        /*!< The code segment array */
};

/**
 * @brief The magic number used to identify the PSS bytecode file header
 * @note  The header indientifer is "\x00\xffpssmod"
 **/
const uint64_t _file_magic = 0x646f6d737370ff00ull;

/**
 * @brief Get the opcode information from the opcode
 * @param opcode the opcode we are interseted in
 * @return the information about the opocde
 **/
static inline const pss_bytecode_info_t* _opcode_info(pss_bytecode_opcode_t opcode)
{
	static int sorted = 0;
	if(!sorted)
	{
		unsigned i,j;
		for(i = 0; i < sizeof(_bytecode) / sizeof(_bytecode[0]); i ++)
		    for(j = i + 1; j < sizeof(_bytecode) / sizeof(_bytecode[0]); j ++)
		        if(_bytecode[i].opcode > _bytecode[j].opcode)
		        {
			        _bytecode_desc_t tmp = _bytecode[i];
			        _bytecode[i] = _bytecode[j];
			        _bytecode[j] = tmp;
		        }
		sorted = 1;
	}
#ifdef __clang__
	if(opcode >= PSS_BYTECODE_OPCODE_COUNT)
#else
	if(opcode >= PSS_BYTECODE_OPCODE_COUNT || opcode < 0)
#endif
	    ERROR_PTR_RETURN_LOG("Invalid instruction opcode = %x", opcode);
	return &_bytecode[opcode].info;
}

/**
 * @brief Dump a instruction to the output file
 * @param inst The instruction to dump
 * @param out The output file pointer
 * @return status code
 **/
static inline int _dump_inst(const _inst_t* inst, FILE* out)
{
	const pss_bytecode_info_t* info = _opcode_info(inst->opcode);

	if(NULL == info) return ERROR_CODE(int);

	if(1 != fwrite(&inst->opcode, sizeof(inst->opcode), 1, out))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the opcode to the output file");

	if(info->has_const && 1 != fwrite(&inst->num, sizeof(inst->num), 1, out))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the const number to the output file");

	if(info->num_regs > 0 && 1 != fwrite(inst->reg, sizeof(pss_bytecode_regid_t) * info->num_regs, 1, out))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the register list to the file");

	return 0;
}

/**
 * @brief Load the instruction from input file
 * @param buf  The buffer for the instruction has been loaded
 * @param in   The input file pointer
 * @return status code
 **/
static inline int _load_inst(_inst_t* buf, FILE* in)
{
	if(1 != fread(&buf->opcode, sizeof(buf->opcode), 1, in))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot read opcode of the instruction");

	const pss_bytecode_info_t* info = _opcode_info(buf->opcode);
	if(NULL == info) return ERROR_CODE(int);

	if(info->has_const && 1 != fread(&buf->num, sizeof(buf->num), 1, in))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot read the number constant from the instruction");

	if(info->num_regs > 0 && 1 != fread(buf->reg, sizeof(pss_bytecode_regid_t) * info->num_regs, 1, in))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot read the register operand list");

	buf->label = ERROR_CODE(pss_bytecode_label_t);

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

	if(size > 0 && 1 != fwrite(str, size, 1, out))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the string content to output file");

	return 0;
}

/**
 * @brief Load string from the output file
 * @param in the input file pointer
 * @return The string loaded from file
 **/
static inline char* _load_string(FILE* in)
{
	uint32_t size;
	if(1 != fread(&size, sizeof(size), 1, in))
	    ERROR_PTR_RETURN_LOG("Cannot read string length from the string table");

	char* ret = (char*)malloc(size + 1);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the string");

	if(size > 0 && 1 != fread(ret, size, 1, in))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read string content from the input file");

	ret[size] = 0;
	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

/**
 * @brief Get the size of each table element for the given type
 * @param Type the type code
 * @return The size of the element
 **/
static inline size_t _table_elem_size(_table_type_t type)
{
	switch(type)
	{
		case _TABLE_TYPE_REG:
		    return sizeof(((_table_t*)NULL)->regid[0]);
		case _TABLE_TYPE_STR:
		    return sizeof(((_table_t*)NULL)->string[0]);
		case _TABLE_TYPE_INST:
		    return sizeof(((_table_t*)NULL)->inst[0]);
		default:
		    return ERROR_CODE(size_t);
	}
}

/**
 * @brief Create a new data table
 * @param cap the initial capacity of the table
 * @param type The type of the table
 * @return the newly created table
 **/
static inline _table_t* _table_new(uint32_t cap, _table_type_t type)
{
	size_t elem_size = _table_elem_size(type);

	if(ERROR_CODE(size_t) == elem_size)
	    ERROR_PTR_RETURN_LOG("Invalid type code");

	_table_t* ret = (_table_t*)malloc(sizeof(_table_t) + elem_size * cap);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the table");

	ret->capacity = cap;
	ret->header.size = 0;
	ret->header.type =  type;

	return ret;
}

/**
 * @brief Make sure the table contains one more space
 * @param talbe The table to ensure
 * @return The pointer has been resized
 **/
static inline _table_t* _table_ensure_space(_table_t* table)
{
	if(table->header.size >= table->capacity)
	{
		size_t elem_size = _table_elem_size(table->header.type);
		_table_t* ret = (_table_t*)realloc(table, sizeof(_table_t) + elem_size * table->capacity * 2);
		if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot resize the table");
		ret->capacity = ret->capacity * 2;
		return ret;
	}

	return table;
}

/**
 * @brief Dispose a used table
 * @param table The table to dispose
 * @return status code
 **/
static inline int _table_free(_table_t* table)
{
	uint32_t i;
	if(table->header.type == _TABLE_TYPE_STR)
	{
		for(i = 0; i < table->header.size; i ++)
		    free(table->string[i]);
	}
	free(table);
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
	if(fwrite(&table->header, sizeof(table->header), 1, out) != 1)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the size of the data table to output file");

	uint32_t i;
	for(i = 0; i < table->header.size; i ++)
	{
		switch(table->header.type)
		{
			case _TABLE_TYPE_STR:
			    if(ERROR_CODE(int) == _dump_string(table->string[i], out))
			        ERROR_RETURN_LOG(int, "Cannot dump string to file");
			    break;
			case _TABLE_TYPE_REG:
			    if(1 != fwrite(table->regid + i, sizeof(table->regid[i]), 1, out))
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
 * @brief Load a table from the bytecode file
 * @param type The type of the table
 * @param in The input file pointer
 * @return The newly loaded table
 **/
static inline _table_t* _load_table(_table_type_t type, FILE* in)
{
	_table_header_t header;
	if(fread(&header, sizeof(header), 1, in) != 1)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot read the size of the table");

	if(header.type != type)
	    ERROR_PTR_RETURN_LOG("Header type mismatch");

	_table_t* ret = _table_new(header.size, type);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot create the table in memory");

	ret->header = header;

	if(type == _TABLE_TYPE_REG)
	{
		if(header.size > 0 && 1 != fread(ret->regid, sizeof(ret->regid[0]) * header.size, 1, in))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the register ids from the register ID table");
	}
	else if(type == _TABLE_TYPE_STR)
	{
		uint32_t i;
		for(i = 0; i < header.size; i ++)
		    if(NULL == (ret->string[i] = _load_string(in)))
		        ERROR_LOG_GOTO(STR_ERR, "Cannot read the string table from the input file");
		goto RET;
STR_ERR:
		for(; i > 0; i --)
		    free(ret->string[i - 1]);
		goto ERR;
	}
	else if(type == _TABLE_TYPE_INST)
	{
		uint32_t i;
		for(i = 0; i < header.size; i ++)
		    if(ERROR_CODE(int) == _load_inst(ret->inst + i , in))
		        ERROR_LOG_GOTO(ERR, "Cannot read instruction from the input file");
	}
	else ERROR_LOG_GOTO(ERR, "Invalid table type");

RET:
	ret->header.size = header.size;
	return ret;
ERR:
	if(NULL != ret) _table_free(ret);
	return NULL;
}

/**
 * @brief Dump an bytecode segment to the bytecode file
 * @param seg The segment to dump
 * @param out The output file pointer
 * @return status code
 **/
static inline int _dump_segment(const pss_bytecode_segment_t* seg, FILE* out)
{
	if(ERROR_CODE(int) == _dump_table(seg->argument_table, out))
	    ERROR_RETURN_LOG(int, "Cannot dump the argument table to the output file");

	if(ERROR_CODE(int) == _dump_table(seg->string_table, out))
	    ERROR_RETURN_LOG(int, "Cannot dump the string table to output file");

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
	if(1 != fwrite(&module->header, sizeof(module->header), 1, out))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the table header to the file");

	uint32_t i;
	for(i = 0; i < module->header.nseg; i ++)
	    if(ERROR_CODE(int) == _dump_segment(module->segs[i], out))
	        ERROR_RETURN_LOG(int, "Cannot dump segment to the bytecode file");

	return 0;
}

int pss_bytecode_module_dump(const pss_bytecode_module_t* module, const char* path)
{
	if(NULL == module || NULL == path) ERROR_RETURN_LOG(int, "Invalid arguments");
	FILE* fp = fopen(path, "wb");
	if(NULL == fp) ERROR_RETURN_LOG_ERRNO(int, "Cannot open file %s for write", path);

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

/**
 * @brief Allocate an empty bytecode module object
 * @param cap The initial capacity of the module
 * @return status code
 **/
static inline pss_bytecode_module_t* _module_new(uint32_t cap)
{
	pss_bytecode_module_t* ret = (pss_bytecode_module_t*)calloc(1, sizeof(pss_bytecode_module_t));

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new module");

	if(NULL == (ret->segs = (pss_bytecode_segment_t**)malloc(sizeof(pss_bytecode_segment_t*) * cap)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the segment table");

	ret->header.magic_num = _file_magic;
	ret->header.nseg = 0;
	ret->capacity = cap;

	return ret;
ERR:
	free(ret);
	return NULL;
}

/**
 * @brief Load segment from the input file
 * @param in the input file pointer
 * @return status code
 **/
static inline pss_bytecode_segment_t* _load_segment(FILE* in)
{
	pss_bytecode_segment_t* ret = (pss_bytecode_segment_t*)calloc(1, sizeof(pss_bytecode_segment_t));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new segment");

	if(NULL == (ret->argument_table = _load_table(_TABLE_TYPE_REG, in)))
	    ERROR_LOG_GOTO(ERR, "Cannot load the register table");

	if(NULL == (ret->string_table = _load_table(_TABLE_TYPE_STR, in)))
	    ERROR_LOG_GOTO(ERR, "Cannot load the string table");

	if(NULL == (ret->code_table = _load_table(_TABLE_TYPE_INST, in)))
	    ERROR_LOG_GOTO(ERR, "Cannot load the instruction table");

	return ret;

ERR:
	if(NULL != ret->argument_table) _table_free(ret->argument_table);

	if(NULL != ret->string_table) _table_free(ret->string_table);

	if(NULL != ret->code_table) _table_free(ret->code_table);

	free(ret);

	return NULL;

}

pss_bytecode_module_t* pss_bytecode_module_new()
{
	return _module_new(32 /* TODO: make this configurable */);
}

pss_bytecode_module_t* pss_bytecode_module_load(const char* path)
{
	uint32_t i = 0;
	if(NULL == path) ERROR_PTR_RETURN_LOG("Invalid arguments");

	FILE* fp = fopen(path, "rb");
	pss_bytecode_module_t* ret = NULL;

	if(NULL == fp) ERROR_PTR_RETURN_LOG_ERRNO("Cannot open file %s for read", path);

	_module_header_t header;

	if(1 != fread(&header, sizeof(header), 1, fp))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the file header");

	if(header.magic_num != _file_magic) ERROR_LOG_ERRNO_GOTO(ERR, "Invalid file format");

	if(NULL == (ret = _module_new(header.nseg)))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the module to load");

	/* Copy the header form the file */
	ret->header = header;
	ret->capacity = header.nseg;

	for(i = 0; i < header.nseg; i ++)
	    if(NULL == (ret->segs[i] = _load_segment(fp)))
	        ERROR_LOG_GOTO(ERR, "Cannot load the segment");

	fclose(fp);
	return ret;
ERR:
	if(NULL != fp) fclose(fp);
	if(NULL != ret)
	{
		uint32_t j;
		for(j = 0; j < i; i ++)
		{
			if(NULL != ret->segs[j]->string_table) _table_free(ret->segs[j]->string_table);
			if(NULL != ret->segs[j]->argument_table) _table_free(ret->segs[j]->argument_table);
			if(NULL != ret->segs[j]->code_table) _table_free(ret->segs[j]->code_table);
			free(ret->segs[i]);
		}

		free(ret->segs);
		free(ret);
	}

	return NULL;
}

int pss_bytecode_module_free(pss_bytecode_module_t* module)
{
	int rc = 0;

	uint32_t i;
	for(i =0 ; i < module->header.nseg; i ++)
	{
		pss_bytecode_segment_t* seg = module->segs[i];
		if(ERROR_CODE(int) == pss_bytecode_segment_free(seg))
		    rc = ERROR_CODE(int);
	}

	free(module->segs);
	free(module);

	return rc;
}

pss_bytecode_segid_t pss_bytecode_module_append(pss_bytecode_module_t* module, pss_bytecode_segment_t* segment)
{
	if(NULL == module || NULL == segment)
	    ERROR_RETURN_LOG(pss_bytecode_segid_t, "Invalid arguments");

	if(module->header.nseg >= module->capacity)
	{
		pss_bytecode_segment_t** new_segs = (pss_bytecode_segment_t**)realloc(module->segs, 2 * module->capacity * sizeof(pss_bytecode_segment_t*));

		if(NULL == new_segs)
		    ERROR_RETURN_LOG_ERRNO(pss_bytecode_segid_t, "Cannot resize the segment");

		module->capacity *= 2;
		module->segs = new_segs;
	}

	pss_bytecode_segid_t ret;

	module->segs[ret = (module->header.nseg ++)] = segment;

	return ret;
}

const pss_bytecode_segment_t* pss_bytecode_module_get_seg(const pss_bytecode_module_t* module, pss_bytecode_segid_t id)
{
	if(NULL == module || ERROR_CODE(pss_bytecode_segid_t) == id)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(module->header.nseg <= id) ERROR_PTR_RETURN_LOG("Invalid segment id");

	return module->segs[id];
}

pss_bytecode_segid_t pss_bytecode_module_get_entry_point(const pss_bytecode_module_t* module)
{
	if(NULL == module)
	    ERROR_RETURN_LOG(pss_bytecode_segid_t, "Invalid arguments");

	return module->header.entry_point;
}

int pss_bytecode_module_set_entry_point(pss_bytecode_module_t* module, pss_bytecode_segid_t id)
{
	if(NULL == module)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	module->header.entry_point = id;

	return 0;
}

pss_bytecode_segment_t* pss_bytecode_segment_new(pss_bytecode_regid_t argc, const pss_bytecode_regid_t* argv)
{
	if(argc > 0 && argv == NULL)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	pss_bytecode_segment_t* ret = (pss_bytecode_segment_t*)calloc(1, sizeof(pss_bytecode_segment_t));

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new segment");

	if(NULL == (ret->argument_table = _table_new((uint32_t)argc, _TABLE_TYPE_REG)))
	    ERROR_LOG_GOTO(ERR, "Cannot create the argument table");

	if(NULL == (ret->string_table = _table_new(32 /* TODO: make this configurable */, _TABLE_TYPE_STR)))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the string table");

	if(NULL == (ret->code_table = _table_new(32 /* TODO: make this configurable */, _TABLE_TYPE_INST)))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate the code table");

	/* Fill the registers */
	memcpy(ret->argument_table->regid, argv, sizeof(pss_bytecode_regid_t) * argc);
	ret->argument_table->header.size = argc;

	return ret;

ERR:
	if(NULL != ret)
	{
		if(NULL != ret->argument_table) _table_free(ret->argument_table);
		if(NULL != ret->string_table)   _table_free(ret->string_table);
		if(NULL != ret->code_table)     _table_free(ret->code_table);
		free(ret);
	}

	return NULL;
}

int pss_bytecode_segment_free(pss_bytecode_segment_t* segment)
{
	if(NULL == segment)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;

	if(NULL != segment->argument_table && ERROR_CODE(int) == _table_free(segment->argument_table))
	    rc = ERROR_CODE(int);

	if(NULL != segment->string_table && ERROR_CODE(int) == _table_free(segment->string_table))
	    rc = ERROR_CODE(int);

	if(NULL != segment->code_table && ERROR_CODE(int) == _table_free(segment->code_table))
	    rc = ERROR_CODE(int);

	free(segment);

	return rc;
}

int pss_bytecode_segment_get_args(const pss_bytecode_segment_t* segment, pss_bytecode_regid_t const** resbuf)
{
	if(NULL == segment) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(resbuf != NULL) *resbuf = segment->argument_table->regid;

	return (int)segment->argument_table->header.size;
}

pss_bytecode_label_t pss_bytecode_segment_label_alloc(pss_bytecode_segment_t* segment)
{
	if(NULL == segment) ERROR_RETURN_LOG(pss_bytecode_label_t, "Invalid arguments");

	return segment->next_label ++;
}

int pss_bytecode_segment_patch_label(pss_bytecode_segment_t* segment, pss_bytecode_label_t label, pss_bytecode_addr_t addr)
{
	if(NULL == segment || ERROR_CODE(pss_bytecode_label_t) == label || ERROR_CODE(pss_bytecode_addr_t) == addr)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_bytecode_addr_t i;
	for(i = 0; i < segment->code_table->header.size; i ++)
	    if(segment->code_table->inst[i].label == label)
	    {
		    segment->code_table->inst[i].label = ERROR_CODE(pss_bytecode_label_t);
		    segment->code_table->inst[i].num   = (pss_bytecode_numeric_t)addr;
	    }

	return 0;
}

pss_bytecode_addr_t  pss_bytecode_segment_append_code(pss_bytecode_segment_t* segment, pss_bytecode_opcode_t opcode, ...)
{
#ifdef __clang__
	if(NULL == segment || opcode >= PSS_BYTECODE_OPCODE_COUNT)
#else
	if(NULL == segment || opcode < 0 || opcode >= PSS_BYTECODE_OPCODE_COUNT)
#endif
	    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Invalid arguments");

	const pss_bytecode_info_t* info = _opcode_info(opcode);

	if(NULL == info) return ERROR_CODE(pss_bytecode_addr_t);

	_table_t* new_table = _table_ensure_space(segment->code_table);

	if(NULL == new_table)
	    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Cannot enlarge the code table");

	segment->code_table = new_table;

	pss_bytecode_addr_t ret = (pss_bytecode_addr_t)segment->code_table->header.size;

	segment->code_table->inst[ret].opcode = opcode;

	_inst_t* inst = segment->code_table->inst + ret;

	uint32_t n_str = 0;
	uint32_t n_num = 0;
	uint32_t n_label = 0;
	uint32_t n_reg = 0;

	va_list ap;
	va_start(ap, opcode);

	pss_bytecode_argtype_t argtype;

	inst->label = ERROR_CODE(pss_bytecode_label_t);

	while(PSS_BYTECODE_ARGTYPE_END != (argtype = va_arg(ap, pss_bytecode_argtype_t)))
	{
		switch(argtype)
		{
			case PSS_BYTECODE_ARGTYPE_REGISTER:
			{
				pss_bytecode_regid_t regid = (pss_bytecode_regid_t)va_arg(ap, int);
				if(regid == ERROR_CODE(pss_bytecode_regid_t))
				    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Invalid register argument");
				if(n_reg >= sizeof(inst->reg) / sizeof(inst->reg[0]))
				    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Too many operands");
				inst->reg[n_reg ++] = regid;
				break;
			}
			case PSS_BYTECODE_ARGTYPE_NUMERIC:
			{
				if(n_num + n_label + n_str > 0) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Too many numeric argument");
				pss_bytecode_numeric_t value = va_arg(ap, pss_bytecode_numeric_t);
				inst->num = value;
				n_num ++;
				break;
			}
			case PSS_BYTECODE_ARGTYPE_LABEL:
			{
				if(n_num + n_label + n_str > 0) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Too many numeric argument");
				pss_bytecode_label_t label = va_arg(ap, pss_bytecode_label_t);
				inst->label = (pss_bytecode_numeric_t)label&0xffffffffu;
				n_label ++;
				break;
			}
			case PSS_BYTECODE_ARGTYPE_STRING:
			{
				if(n_num + n_label + n_str > 0) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Too many string argument");
				const char* str = va_arg(ap, const char*);

				if(NULL == str) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Invalid string");

				_table_t* new_table = _table_ensure_space(segment->string_table);
				if(NULL == new_table)
				    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Cannot enlarge the string table");

				segment->string_table = new_table;

				pss_bytecode_numeric_t strid = (pss_bytecode_numeric_t)segment->string_table->header.size;
				size_t len = strlen(str) + 1;

				if(NULL == (segment->string_table->string[strid] = (char*)malloc(len)))
				    ERROR_RETURN_LOG_ERRNO(pss_bytecode_addr_t, "Cannot allocate memory for the new string");

				memcpy(segment->string_table->string[strid], str, len);
				segment->string_table->header.size ++;

				inst->num = strid;
				n_str ++;
				break;
			}
			default:
			    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Invalid argument type");
		}
	}
	va_end(ap);

	/* Finally we should validate the instruction is well formed */
	if(n_reg != info->num_regs)
	    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Wrong number of register arguments");

	if(((info->string_ref && info->has_const) ? 1 : 0) != n_str)
	    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Wrong number of string arguments");

	if(((info->has_const && !info->string_ref) ? 1 : 0) != (n_num + n_label))
	    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Wrong number of numeric arguments or label");

	segment->code_table->header.size ++;

	return ret;
}

pss_bytecode_addr_t  pss_bytecode_segment_length(const pss_bytecode_segment_t* segment)
{
	if(NULL == segment)
	    ERROR_RETURN_LOG(pss_bytecode_addr_t, "Invalid arguments");

	return (pss_bytecode_addr_t)segment->code_table->header.size;
}

int pss_bytecode_segment_get_inst(const pss_bytecode_segment_t* segment, pss_bytecode_addr_t addr, pss_bytecode_instruction_t* buf)
{
	if(NULL == segment || ERROR_CODE(pss_bytecode_addr_t) == addr || segment->code_table->header.size <= addr || NULL == buf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _inst_t* inst = segment->code_table->inst + addr;
	if(NULL == inst) ERROR_RETURN_LOG(int, "Invalid address");

	buf->opcode = inst->opcode;
	if(NULL == (buf->info = _opcode_info(buf->opcode)))
	    ERROR_RETURN_LOG(int, "Invalid opcode 0x%x", buf->opcode);

	buf->num = 0;
	buf->str = NULL;

	if(buf->info->has_const)
	{
		pss_bytecode_numeric_t num = inst->num;

		if(buf->info->string_ref)
		{
			if(num < 0 || num >= segment->string_table->header.size)
			    ERROR_RETURN_LOG(int, "Invalid string ID");
			buf->str = segment->string_table->string[num];
		}
		else
		    buf->num = num;
	}

	uint32_t i;
	for(i = 0; i < buf->info->num_regs; i ++)
	    buf->reg[i] = inst->reg[i];

	return 0;
}

const char* pss_bytecode_segment_inst_str(const pss_bytecode_segment_t* segment, pss_bytecode_addr_t addr, char* buf, size_t sz)
{
	if(NULL == segment || ERROR_CODE(pss_bytecode_addr_t) == addr || segment->code_table->header.size <= addr || buf == NULL)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	pss_bytecode_instruction_t inst;

	if(ERROR_CODE(int) == pss_bytecode_segment_get_inst(segment, addr, &inst))
	    ERROR_PTR_RETURN_LOG("Cannot get instruction from the code segment");

	size_t used = 0;

#define _WRITE_BUFFER(fmtstr, args... ) do {\
		int rc = snprintf(buf + used, sz - used, fmtstr, ##args);\
		if(rc < 0) ERROR_PTR_RETURN_LOG("Cannot dump instruction string to memory");\
		if((size_t)rc > sz - used) rc = (int)(sz - used);\
		used += (size_t)rc;\
	} while(0)

	_WRITE_BUFFER("%s", inst.info->name);
	if(inst.info->has_const)
	{
		if(inst.info->string_ref)
		    _WRITE_BUFFER("(%s)", inst.str);
		else
		    _WRITE_BUFFER("(%"PRId64")", inst.num);
	}

	while(used < 20) _WRITE_BUFFER(" ");
	_WRITE_BUFFER(" ");

	uint32_t i;
	for(i = 0; i < inst.info->num_regs; i ++)
	{
		if(i) _WRITE_BUFFER("%s", ", ");
		_WRITE_BUFFER("R%"PRId16, inst.reg[i]);
	}

	return buf;
}

int pss_bytecode_segment_logdump(const pss_bytecode_segment_t* segment)
{
	if(NULL == segment)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

#ifdef LOG_INFO_ENABLED
	char buf[1024] = {0};

	uint32_t i, start = 0;
	for(i = 0; i < segment->argument_table->header.size; i ++)
	    start += (uint32_t)snprintf(buf + start, sizeof(buf) > start ? sizeof(buf) - start : 0, i ? " R%"PRIu16 : "R%"PRIu16, segment->argument_table->regid[i]);
	LOG_INFO("+Segment(%s)", buf);

	for(i = 0; i < segment->code_table->header.size; i ++)
	    LOG_INFO("++0x%.6x %s", i, pss_bytecode_segment_inst_str(segment, i, buf, sizeof(buf)));
#endif

	return 0;
}

int pss_bytecode_module_logdump(const pss_bytecode_module_t* module)
{
	if(NULL == module)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

#ifdef LOG_INFO_ENABLED
	LOG_INFO("Entry Point: %u", module->header.entry_point);

	uint32_t i;
	for(i = 0; i < module->header.nseg; i ++)
	    pss_bytecode_segment_logdump(module->segs[i]);

#endif
	return 0;
}
