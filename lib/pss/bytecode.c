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
 * @brief The type of the table 
 **/
typedef enum {
	_TABLE_TYPE_STR,
	_TABLE_TYPE_REG,
	_TABLE_TYPE_INST
} _table_type_t;

/**
 * @brief The data structure for internal data tables
 **/
typedef struct {
	uint32_t      capacity;   /*!< The capacity of the string table */
	uint32_t      size;       /*!< The actual size of the string table */
	_table_type_t type;               /*!< What kind of table it is */
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
	_table_t*         argument_table; /*!< The argument table, which indicates what register needs to be initialied by the callee */
	_table_t*         string_table;   /*!< The string constant table for this code segment */
	_table_t*         code_table;     /*!< The table of the function body */
};

/**
 * @brief The module file header
 **/
typedef struct {
	uint64_t magic_num;    /*!< The magic number */
	uint32_t nseg;         /*!< How many segments in the module */
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
 **/
const uint64_t _file_magic = 0x76737065ffffffull;

/**
 * @brief Get the opcode information from the opcode
 * @param opcode the opcode we are interseted in
 * @return the information about the opocde
 **/
static inline const pss_bytecode_info_t* _opcode_info(pss_bytecode_opcode_t opcode)
{
	if(opcode >= PSS_BYTECODE_OPCODE_COUNT || opcode < 0)
		ERROR_PTR_RETURN_LOG("Invalid instruction opcode = %x", opcode);
	return &_bytecode[opcode].info;
}

/**
 * @brief Dump a instruction to the output file
 * @param inst The instruction to dump
 * @param out The output file pointer
 * @return status code
 **/
static inline int _dump_inst(const pss_bytecode_inst_t* inst, FILE* out)
{
	const pss_bytecode_info_t* info = _opcode_info(inst->opcode);

	if(NULL == info) return ERROR_CODE(int);

	if(1 != fwrite(&inst->opcode, sizeof(inst->opcode), 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the opcode to the output file");
	if(info->has_const && 1 != fwrite(&inst->num, sizeof(inst->num), 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the const number to the output file");

	if(1 != fwrite(inst->reg, sizeof(pss_bytecode_regid_t) * info->num_regs, 1, out))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot dump the register list to the file");

	return 0;
}

/**
 * @brief Load the instruction from input file
 * @param buf  The buffer for the instruction has been loaded
 * @param in   The input file pointer
 * @return status code
 **/
static inline int _load_inst(pss_bytecode_inst_t* buf, FILE* in)
{
	if(1 != fread(&buf->opcode, sizeof(buf->opcode), 1, in))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot read opcode of the instruction");

	const pss_bytecode_info_t* info = _opcode_info(buf->opcode);
	if(NULL == info) return ERROR_CODE(int);

	if(info->has_const && 1 != fread(&buf->num, sizeof(buf->num), 1, in))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot read the number constant from the instruction");

	if(1 != fread(buf->reg, sizeof(pss_bytecode_regid_t) * info->num_regs, 1, in))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot read the register operand list");

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

	if(1 != fread(ret, size, 1, in))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read string content from the input file");

	ret[size] = 0;
	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

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
	_table_t* ret = (_table_t*)malloc(sizeof(_table_t) + elem_size * cap);

	if(NULL == ret) 
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the table");

	ret->capacity = cap;
	ret->size = 0;
	ret->type =  type;

	return ret;
}

/**
 * @brief Make sure the table contains one more space
 * @param talbe The table to ensure
 * @return The pointer has been resized
 **/
static inline _table_t* _table_ensure_space(_table_t* table)
{
	if(table->size >= table->capacity)
	{
		size_t elem_size = _table_elem_size(table->type);
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
	for(i = 0; i < table->size; i ++)
		if(table->type == _TABLE_TYPE_STR)
		{
			free(table->string[i]);
			break;
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
 * @brief Load a table from the bytecode file
 * @param type The type of the table
 * @param in The input file pointer
 * @return The newly loaded table
 **/
static inline _table_t* _load_table(_table_type_t type, FILE* in)
{
	uint32_t size;
	if(fread(&size, sizeof(size), 1, in) != 1)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot read the size of the table");

	_table_t* ret = _table_new(size, type);
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot create the table in memory");

	if(type == _TABLE_TYPE_REG)
	{
		if(1 != fread(ret->regid, sizeof(ret->regid[0]) * size, 1, in))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the register ids from the register ID table");
	}
	else if(type == _TABLE_TYPE_STR)
	{
		uint32_t i;
		for(i = 0; i < size; i ++)
			if(NULL == (ret->string[i] = _load_string(in)))
				ERROR_LOG_ERRNO_GOTO(STR_ERR, "Cannot read the string table from the input file");
		goto RET;
STR_ERR:
		for(; i > 0; i --)
			free(ret->string[i - 1]);
		goto ERR;
	}
RET:
	ret->size = size;
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
	pss_bytecode_segment_t* ret = (pss_bytecode_segment_t*)malloc(sizeof(pss_bytecode_segment_t));
	if(NULL == ret) 
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new segment");

	ret->string_table = NULL;
	ret->code_table = NULL;
	ret->argument_table = NULL;

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

	FILE* fp = fopen(path, "wb");
	pss_bytecode_module_t* ret = NULL;

	if(NULL == fp) ERROR_PTR_RETURN_LOG_ERRNO("Cannot open file %s for read", path);

	_module_header_t header;

	if(1 == fread(&header, sizeof(header), 1, fp))
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
	int rc;

	uint32_t i;
	for(i =0 ; i < module->header.nseg; i ++)
	{
		pss_bytecode_segment_t* seg = module->segs[i];

		if(NULL != seg)
		{
			if(NULL != seg->string_table && ERROR_CODE(int) == _table_free(seg->string_table))
				rc = ERROR_CODE(int);

			if(NULL != seg->argument_table && ERROR_CODE(int) == _table_free(seg->argument_table))
				rc = ERROR_CODE(int);

			if(NULL != seg->code_table && ERROR_CODE(int) == _table_free(seg->code_table))
				rc = ERROR_CODE(int);
		}

		free(seg);
	}

	free(module);

	return rc;
}

