/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <plumber.h>
#include <utils/vector.h>
#include <utils/log.h>
#include <utils/string.h>
#include <utils/hashmap.h>
#include <error.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
/**
 * @brief the human readable instuction name
 **/
static const char* _opcode_str[] = {
	[LANG_BYTECODE_OPCODE_MOVE]    = "move",
	[LANG_BYTECODE_OPCODE_PUSHARG] = "pusharg",
	[LANG_BYTECODE_OPCODE_INVOKE]  = "invoke",
	[LANG_BYTECODE_OPCODE_MOD]     = "mod",
	[LANG_BYTECODE_OPCODE_SUB]     = "sub",
	[LANG_BYTECODE_OPCODE_ADD]     = "add",
	[LANG_BYTECODE_OPCODE_MUL]     = "mul",
	[LANG_BYTECODE_OPCODE_DIV]     = "div",
	[LANG_BYTECODE_OPCODE_JUMP]    = "jump",
	[LANG_BYTECODE_OPCODE_AND]     = "and",
	[LANG_BYTECODE_OPCODE_OR]      = "or",
	[LANG_BYTECODE_OPCODE_XOR]     = "xor",
	[LANG_BYTECODE_OPCODE_LT]      = "lt",
	[LANG_BYTECODE_OPCODE_LE]      = "le",
	[LANG_BYTECODE_OPCODE_GT]      = "gt",
	[LANG_BYTECODE_OPCODE_GE]      = "ge",
	[LANG_BYTECODE_OPCODE_EQ]      = "eq",
	[LANG_BYTECODE_OPCODE_NE]      = "ne",
	[LANG_BYTECODE_OPCODE_JZ]      = "jz",
	[LANG_BYTECODE_OPCODE_UNDEFINED] = "moveundef"
};
/**
 * @brief the human readable build-in function name
 **/
static const char* _builtin_str[] = {
	[LANG_BYTECODE_BUILTIN_NEW_GRAPH]  = "new_graph",
	[LANG_BYTECODE_BUILTIN_ECHO]       = "echo",
	[LANG_BYTECODE_BUILTIN_INPUT]      = "set_input",
	[LANG_BYTECODE_BUILTIN_OUTPUT]     = "set_output",
	[LANG_BYTECODE_BUILTIN_START]      = "start",
	[LANG_BYTECODE_BUILTIN_GRAPHVIZ]   = "graphviz",
	[LANG_BYTECODE_BUILTIN_ADD_NODE]   = "add_node",
	[LANG_BYTECODE_BUILTIN_ADD_EDGE]   = "add_edge",
	[LANG_BYTECODE_BUILTIN_INSMOD]     = "insmod"
};

/**
 * @brief the data table
 **/
typedef struct {
	vector_t*  vector;    /*!< the reverse vector */
	hashmap_t* hashmap;   /*!< hashmap */
} _data_table_t;

/**
 * @brief represents a bytecode
 **/
typedef struct {
	uint8_t opcode;          /*!< the opcode of the bytecode */
	uint8_t operand_type[3]; /*!< the typecode for the operands */
	union {
		uint32_t u;          /*!< the unsigned data */
		int32_t  i;          /*!< the signed data */
	} operands[3];
} __attribute__((packed)) _bytecode_t;

/**
 * @brief the actual data structure for the bytecode table
 **/
struct _lang_bytecode_table_t {
	uint32_t    reg_count;       /*!< the number of register */
	_data_table_t  string_table;    /*!< the string table */
	_data_table_t  symbol_table;    /*!< the symbol table */
	vector_t*   bytecodes;       /*!< the bytecode list */
};

/**
 * @brief the actuall data structure for the label table
 **/
struct _lang_bytecode_label_table_t {
	vector_t* address_table;   /*!< the address table */
};

/**
 * @brief append the data table entry to the reverse lookup vector
 * @param table the target data table
 * @param result the result
 * @return status code
 **/
static inline int _append_reverse_vector(_data_table_t* table, const hashmap_find_res_t* result)
{
	vector_t* new = vector_append(table->vector, result);

	if(NULL == new) ERROR_RETURN_LOG(int, "Cannot append new entry to the vector");

	table->vector = new;

	return 0;
}

/**
 * @brief get the string id from the bytecode table
 * @param tab the target bytecode table
 * @param string the string to query
 * @return status code
 **/
static inline uint32_t _get_string_id(lang_bytecode_table_t* tab, const char* string)
{
	size_t len = strlen(string) + 1;

	hashmap_find_res_t result;
	uint32_t next_id = (uint32_t)vector_length(tab->string_table.vector);
	int rc;

	if(ERROR_CODE(int) == (rc = hashmap_insert(tab->string_table.hashmap, string, len, &next_id, sizeof(uint32_t), &result, 0)))
	    ERROR_RETURN_LOG(uint32_t, "Error during hashmap operation");

	if(rc == 1)
	{
		LOG_DEBUG("Created new string table entry `%s'", string);
		if(_append_reverse_vector(&tab->string_table, &result) == ERROR_CODE(int))
		    ERROR_RETURN_LOG(uint32_t, "Cannot update the reverse vector");
	}

	return *(const uint32_t*)result.val_data;
}

/**
 * @brief append a bytecode to the table
 * @param tab the target bytecode table
 * @param opcode the opcode of the instruction
 * @param operands a list of operands, ends with NULL
 * @note there's at most 3 operands for each bytecode
 * @return status code
 **/
static inline int _append_bytecode(lang_bytecode_table_t* tab, lang_bytecode_opcode_t opcode,
                                   lang_bytecode_operand_t const* const* operands)
{
	if(NULL == tab) ERROR_RETURN_LOG(int, "Invalid arguments");

	_bytecode_t buffer;

	int i;
	buffer.opcode = opcode;
	buffer.operand_type[0] = buffer.operand_type[1] = buffer.operand_type[2] = 0xff;
	buffer.operands[0].u = buffer.operands[1].u = buffer.operands[2].u = 0;
	for(i = 0; i < 3 && operands[i] != NULL; i ++)
	{
		uint32_t id;
		switch(operands[i]->type)
		{
			case LANG_BYTECODE_OPERAND_STR:
			case LANG_BYTECODE_OPERAND_GRAPHVIZ:
			    if(ERROR_CODE(uint32_t) == (id = _get_string_id(tab, operands[i]->str)))
			        ERROR_RETURN_LOG(int, "Cannot find a id for the string");
			    break;
			case LANG_BYTECODE_OPERAND_STRID:
			    if(ERROR_CODE(uint32_t) == (id = operands[i]->strid))
			        ERROR_RETURN_LOG(int, "Invalid arguments");
			    break;
			case LANG_BYTECODE_OPERAND_SYM:
			{
				uint32_t j;
				for(j = 0; operands[i]->sym[j] != ERROR_CODE(uint32_t); j ++);

				hashmap_find_res_t result;
				uint32_t next_id = (uint32_t)vector_length(tab->symbol_table.vector);
				int rc;
				if((rc = hashmap_insert(tab->symbol_table.hashmap,
				                        operands[i]->sym, sizeof(uint32_t) * j,
				                        &next_id, sizeof(uint32_t), &result, 0)) == ERROR_CODE(int))
				    ERROR_RETURN_LOG(int, "Cannot access the symbol table");

				if(1 == rc && _append_reverse_vector(&tab->symbol_table, &result) == ERROR_CODE(int))
				    ERROR_RETURN_LOG(int, "Cannot update the reverse lookup table");

				id = *(uint32_t*)result.val_data;
				break;
			}
			case LANG_BYTECODE_OPERAND_REG:
			    id = operands[i]->reg;
			    if(tab->reg_count <= id) tab->reg_count = id + 1;
			    break;
			case LANG_BYTECODE_OPERAND_BUILTIN:
			    id = (uint32_t)operands[i]->builtin;
			    break;
			case LANG_BYTECODE_OPERAND_INT:
			    id = (uint32_t)operands[i]->num;
			    break;
			case LANG_BYTECODE_OPERAND_LABEL:
			    id = (uint32_t)operands[i]->label;
			    break;
			default:
			    ERROR_RETURN_LOG(int, "Invalid type of operands");
		}
		buffer.operands[i].u = id;
		buffer.operand_type[i] = operands[i]->type;
		if(buffer.operand_type[i] == LANG_BYTECODE_OPERAND_STRID)
		    buffer.operand_type[i] = LANG_BYTECODE_OPERAND_STR;
	}

	vector_t* new = vector_append(tab->bytecodes, &buffer);
	if(NULL == new)
	    ERROR_RETURN_LOG(int, "Cannot append new instruction to bytecode table");

	tab->bytecodes = new;

	return 0;
}

/**
 * @brief parse the symbol (for example "a.b.c" ) to an array of symbol id
 * @param table the bytecode table
 * @param str the symbol string
 * @param symbol_ids the symbol id list
 * @param buffer_size size of the symbol_ids array
 * @return the number of sections in the symbol
 **/
static inline uint32_t _parse_symbol(lang_bytecode_table_t* table, const char* str, uint32_t* symbol_ids, size_t buffer_size)
{
	static char buffer[4096];
	uint32_t nsymbol = 0;
	const char* ptr;
	char* out_ptr = buffer;
	size_t sz = sizeof(buffer) - 1;
	int warnned = 0;
	for(ptr = str;;)
	{
		if(*ptr == 0 || *ptr == '.')
		{
			size_t symbol_length = (size_t)(out_ptr - buffer);
			if(symbol_length > 0)
			{
				*out_ptr = 0;
				uint32_t symbol_id = lang_bytecode_table_acquire_string_id(table, buffer);
				if(ERROR_CODE(uint32_t) == symbol_id) ERROR_RETURN_LOG(uint32_t, "Cannot acquire the id for the string");
				if(buffer_size > 0)
				    symbol_ids[nsymbol ++] = symbol_id, buffer_size --;
				else
				    LOG_WARNING("symbol contains too many sections, truncated");
				sz = sizeof(buffer) - 1;
				out_ptr = buffer;
			}
			if(*ptr == 0) break;
			else ptr ++;
		}
		if(sz > 0)
		    *(out_ptr++) = *(ptr++), sz --;
		else if(!warnned)
		{
			LOG_WARNING("symbol truncated");
			warnned = 1;
		}
	}
	return nsymbol;
}

/**
 * @brief read the bytecode from the bytecode table
 * @param table the target table
 * @param offset the instruction offset
 * @return the result bytecode, NULL on error case
 **/
static inline const _bytecode_t* _read_bytecode(const lang_bytecode_table_t* table, uint32_t offset)
{
	if(NULL == table || vector_length(table->bytecodes) <= offset) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return VECTOR_GET_CONST(_bytecode_t, table->bytecodes, offset);
}

uint32_t lang_bytecode_table_insert_symbol(lang_bytecode_table_t* table, const char* str)
{
	if(NULL == table || NULL == str) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");
	static uint32_t symbol_ids[4096];
	uint32_t n_ids = _parse_symbol(table, str, symbol_ids, sizeof(symbol_ids) / sizeof(symbol_ids[0]));
	if(ERROR_CODE(uint32_t) == n_ids) ERROR_RETURN_LOG(uint32_t, "Cannot parse symbol");

	hashmap_find_res_t result;
	uint32_t next_id = (uint32_t)vector_length(table->symbol_table.vector);
	int rc;
	if((rc = hashmap_insert(table->symbol_table.hashmap,
	                        symbol_ids, sizeof(uint32_t) * n_ids,
	                        &next_id, sizeof(uint32_t), &result, 0)) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(uint32_t, "Cannot access the symbol table");

	if(1 == rc && _append_reverse_vector(&table->symbol_table, &result) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(uint32_t, "Cannot update the reverse lookup table");
	return *(uint32_t*)result.val_data;
}

lang_bytecode_table_t* lang_bytecode_table_new()
{
	lang_bytecode_table_t* ret = (lang_bytecode_table_t*)calloc(1, sizeof(lang_bytecode_table_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the bytecode table");

	if(NULL == (ret->string_table.hashmap = hashmap_new(LANG_BYTECODE_HASH_SIZE, LANG_BYTECODE_HASH_POOL_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create hashmap for string table");

	if(NULL == (ret->string_table.vector = vector_new(sizeof(hashmap_find_res_t), 32)))
	    ERROR_LOG_GOTO(ERR, "Cannot create reverse lookup vector for string table");

	if(NULL == (ret->symbol_table.hashmap = hashmap_new(LANG_BYTECODE_HASH_SIZE, LANG_BYTECODE_HASH_POOL_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create hashmap for the symbol table");

	if(NULL == (ret->symbol_table.vector = vector_new(sizeof(hashmap_find_res_t), 32)))
	    ERROR_LOG_GOTO(ERR,"Cannot create reverse lookup vector for the symbol table");

	if(NULL == (ret->bytecodes = vector_new(sizeof(_bytecode_t), LANG_BYTECODE_LIST_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create the bytecode list");

	return ret;
ERR:
	if(NULL != ret->bytecodes) vector_free(ret->bytecodes);
	if(NULL != ret->string_table.hashmap) hashmap_free(ret->string_table.hashmap);
	if(NULL != ret->string_table.vector)  vector_free(ret->string_table.vector);
	if(NULL != ret->symbol_table.hashmap) hashmap_free(ret->symbol_table.hashmap);
	if(NULL != ret->symbol_table.vector) vector_free(ret->symbol_table.vector);
	free(ret);
	return NULL;
}

int lang_bytecode_table_free(lang_bytecode_table_t* table)
{
	if(NULL == table) ERROR_RETURN_LOG(int, "Invalid arguments");
	int rc = 0;

	if(NULL != table->string_table.hashmap && ERROR_CODE(int) == hashmap_free(table->string_table.hashmap))
	    rc = ERROR_CODE(int);

	if(NULL != table->string_table.vector && ERROR_CODE(int) == vector_free(table->string_table.vector))
	    rc = ERROR_CODE(int);

	if(NULL != table->symbol_table.hashmap && ERROR_CODE(int) == hashmap_free(table->symbol_table.hashmap))
	    rc = ERROR_CODE(int);

	if(NULL != table->symbol_table.vector && ERROR_CODE(int) == vector_free(table->symbol_table.vector))
	    rc = ERROR_CODE(int);

	if(NULL != table->bytecodes && vector_free(table->bytecodes) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	free(table);

	return rc;
}

uint32_t lang_bytecode_table_get_string_id(const lang_bytecode_table_t* table, const char* str)
{
	if(NULL == table || NULL == str) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");
	hashmap_find_res_t result;
	int rc;

	if(ERROR_CODE(int) == (rc = hashmap_find(table->string_table.hashmap, str, strlen(str) + 1, &result)))
	    ERROR_RETURN_LOG(uint32_t, "Error when searching the hashmap");

	if(rc == 0) return ERROR_CODE(uint32_t);

	return *(uint32_t*)result.val_data;
}

uint32_t lang_bytecode_table_acquire_string_id(lang_bytecode_table_t* table, const char* str)
{
	if(NULL == table || NULL == str) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	uint32_t ret = _get_string_id(table, str);
	if(ERROR_CODE(uint32_t) == ret) ERROR_RETURN_LOG(uint32_t, "Cannot acquire new string id for string `%s'", str);

	return ret;
}

int lang_bytecode_table_append_move(lang_bytecode_table_t* table, const lang_bytecode_operand_t* dest, const lang_bytecode_operand_t* src)
{
	if(NULL == table || NULL == dest || NULL == src) ERROR_RETURN_LOG(int, "Invalid arguments");

	lang_bytecode_operand_t const * oplist[] = {dest, src, NULL};

	if(LANG_BYTECODE_OPERAND_REG == dest->type)
	{
		switch(src->type)
		{
			case LANG_BYTECODE_OPERAND_REG:
			case LANG_BYTECODE_OPERAND_STR:
			case LANG_BYTECODE_OPERAND_GRAPHVIZ:
			case LANG_BYTECODE_OPERAND_INT:
			case LANG_BYTECODE_OPERAND_SYM:
			case LANG_BYTECODE_OPERAND_STRID:
			    break;
			default:
			    ERROR_RETURN_LOG(int, "Invalid operand combination");
		}
	}
	else if(LANG_BYTECODE_OPERAND_SYM != dest->type || LANG_BYTECODE_OPERAND_REG != src->type)
	    ERROR_RETURN_LOG(int, "Invalid operand combination");

	if(_append_bytecode(table, LANG_BYTECODE_OPCODE_MOVE, oplist) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot append the bytecode to the bytecode list");

	return 0;
}

int lang_bytecode_table_append_arithmetic(lang_bytecode_table_t* table, lang_bytecode_opcode_t opcode,
                                          const lang_bytecode_operand_t* dest, const lang_bytecode_operand_t* left,
                                          const lang_bytecode_operand_t* right)
{
	if(NULL == table || NULL == dest || NULL == left || NULL == right) ERROR_RETURN_LOG(int, "Invalid arguments");

	switch(opcode)
	{
		case LANG_BYTECODE_OPCODE_ADD:
		case LANG_BYTECODE_OPCODE_SUB:
		case LANG_BYTECODE_OPCODE_MUL:
		case LANG_BYTECODE_OPCODE_DIV:
		case LANG_BYTECODE_OPCODE_MOD:
		case LANG_BYTECODE_OPCODE_AND:
		case LANG_BYTECODE_OPCODE_OR:
		case LANG_BYTECODE_OPCODE_XOR:
		case LANG_BYTECODE_OPCODE_LT:
		case LANG_BYTECODE_OPCODE_LE:
		case LANG_BYTECODE_OPCODE_GT:
		case LANG_BYTECODE_OPCODE_GE:
		case LANG_BYTECODE_OPCODE_NE:
		case LANG_BYTECODE_OPCODE_EQ:
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid opcode");
	}

	if(dest->type != LANG_BYTECODE_OPERAND_REG && dest->type != LANG_BYTECODE_OPERAND_SYM)
	    ERROR_RETURN_LOG(int, "Invalid destination operand");

	if(opcode == LANG_BYTECODE_OPCODE_SUB ||
	   opcode == LANG_BYTECODE_OPCODE_MUL ||
	   opcode == LANG_BYTECODE_OPCODE_DIV ||
	   opcode == LANG_BYTECODE_OPCODE_MOD)
	{
		if((left->type != LANG_BYTECODE_OPERAND_SYM && left->type != LANG_BYTECODE_OPERAND_INT && left->type != LANG_BYTECODE_OPERAND_REG) ||
		   (right->type != LANG_BYTECODE_OPERAND_SYM && right->type != LANG_BYTECODE_OPERAND_INT && right->type != LANG_BYTECODE_OPERAND_REG))
		    ERROR_RETURN_LOG(int, "Invalid operand combination");
	}

	lang_bytecode_operand_t const * oplist[] = {dest, left, right};

	if(_append_bytecode(table, opcode, oplist) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot append the bytecode to the bytecode list");

	return 0;
}

int lang_bytecode_table_append_undefined(lang_bytecode_table_t* table, const lang_bytecode_operand_t* dest)
{
	if(NULL == table || NULL == dest) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(dest->type != LANG_BYTECODE_OPERAND_REG && dest->type != LANG_BYTECODE_OPERAND_SYM)
	    ERROR_RETURN_LOG(int, "Invalid destination operand");

	lang_bytecode_operand_t const * oplist[] = {dest, NULL, NULL};

	if(_append_bytecode(table, LANG_BYTECODE_OPCODE_UNDEFINED, oplist) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot append the bytecode to the bytecode table");

	return 0;
}

int lang_bytecode_table_append_invoke(lang_bytecode_table_t* table, const lang_bytecode_operand_t* reg, const lang_bytecode_operand_t* func)
{
	if(NULL == table || NULL == func) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(LANG_BYTECODE_OPERAND_REG != reg->type) ERROR_RETURN_LOG(int, "Invalid operand combination: register expected");

	if(LANG_BYTECODE_OPERAND_BUILTIN != func->type) ERROR_RETURN_LOG(int, "Invalid operand combination: builtin function id expected");

	lang_bytecode_operand_t const* ops[] = {reg, func, NULL};

	if(_append_bytecode(table, LANG_BYTECODE_OPCODE_INVOKE, ops) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot append the bytecode to the bytecode table");

	return 0;
}

int lang_bytecode_table_append_jump(lang_bytecode_table_t* table, const lang_bytecode_operand_t* target)
{
	if(NULL == table || NULL == target) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(LANG_BYTECODE_OPERAND_REG != target->type &&
	   LANG_BYTECODE_OPERAND_INT != target->type &&
	   LANG_BYTECODE_OPERAND_LABEL != target->type)
	    ERROR_RETURN_LOG(int, "Invalid operand combination: register or int/label expected");

	lang_bytecode_operand_t const* ops[] = {target, NULL, NULL};

	if(_append_bytecode(table, LANG_BYTECODE_OPCODE_JUMP, ops) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot append the byteocde to bytecode table");

	return 0;
}

int lang_bytecode_table_append_jz(lang_bytecode_table_t* table, const lang_bytecode_operand_t* cond, const lang_bytecode_operand_t* target)
{
	if(NULL == table || NULL == target || NULL == cond) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(LANG_BYTECODE_OPERAND_REG != target->type &&
	   LANG_BYTECODE_OPERAND_INT != target->type &&
	   LANG_BYTECODE_OPERAND_LABEL != target->type)
	    ERROR_RETURN_LOG(int, "Invalid operand combination: register or int/label expected");

	lang_bytecode_operand_t const* ops[] = {cond, target, NULL};

	if(_append_bytecode(table, LANG_BYTECODE_OPCODE_JZ, ops) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot append the bytecode to bytecode table");

	return 0;
}

int lang_bytecode_table_append_pusharg(lang_bytecode_table_t* table, const lang_bytecode_operand_t* reg)
{
	if(NULL == table || NULL == reg) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(LANG_BYTECODE_OPERAND_REG != reg->type) ERROR_RETURN_LOG(int, "Invalid operand combination: register expected");

	lang_bytecode_operand_t const* ops[] = {reg, NULL};

	if(_append_bytecode(table, LANG_BYTECODE_OPCODE_PUSHARG, ops) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot append the bytecode to the bytecode table");

	return 0;
}

int lang_bytecode_table_get_opcode(const lang_bytecode_table_t* table, uint32_t offset)
{
	const _bytecode_t* bc = _read_bytecode(table, offset);
	if(NULL == bc) ERROR_RETURN_LOG(int, "Cannot read the bytecode table");

	return bc->opcode;
}

uint32_t  lang_bytecode_table_get_num_operand(const lang_bytecode_table_t* table, uint32_t offset)
{
	const _bytecode_t* bc = _read_bytecode(table, offset);
	if(NULL == bc) ERROR_RETURN_LOG(uint32_t, "Cannot read the bytecode table");
	if(bc->operand_type[2] != 0xff) return 3;
	if(bc->operand_type[1] != 0xff) return 2;
	if(bc->operand_type[0] != 0xff) return 1;
	return 0;
}

int lang_bytecode_table_get_operand(const lang_bytecode_table_t* table, uint32_t offset, uint32_t n, lang_bytecode_operand_id_t* result)
{
	if(NULL == result) ERROR_RETURN_LOG(int, "Invalid arguments");

	const _bytecode_t* bc = _read_bytecode(table, offset);
	if(NULL == bc) ERROR_RETURN_LOG(int, "Cannot read the bytecode table");

	if(n >= 3 || bc->operands[n].u == 0xff) ERROR_RETURN_LOG(int, "Invalid operand id");

	result->type = (lang_bytecode_operand_type_t)bc->operand_type[n];
	result->id = bc->operands[n].u;

	return 0;
}

const char* lang_bytecode_table_str_id_to_string(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id)
{
	if(NULL == table || (id.type != LANG_BYTECODE_OPERAND_STR &&
	                     id.type != LANG_BYTECODE_OPERAND_GRAPHVIZ)) ERROR_PTR_RETURN_LOG("Invalid arguments");

	const hashmap_find_res_t* ret = VECTOR_GET_CONST(hashmap_find_res_t, table->string_table.vector, id.id);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("No such entry in the string table");

	return (const char*)ret->key_data;
}

uint32_t lang_bytecode_table_sym_id_length(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id)
{
	if(NULL == table || id.type != LANG_BYTECODE_OPERAND_SYM) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	const hashmap_find_res_t* ret = VECTOR_GET_CONST(hashmap_find_res_t, table->symbol_table.vector, id.id);
	if(NULL == ret) ERROR_RETURN_LOG(uint32_t, "No such entry in the symbol table");

	return (uint32_t)(ret->key_size / sizeof(uint32_t));
}

const uint32_t* lang_bytecode_table_sym_id_to_strid_array(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id)
{
	if(NULL == table || id.type != LANG_BYTECODE_OPERAND_SYM) ERROR_PTR_RETURN_LOG("Invalid arguments");

	const hashmap_find_res_t* ret = VECTOR_GET_CONST(hashmap_find_res_t, table->symbol_table.vector, id.id);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("No such entry in the symbol table");

	return (const uint32_t*)ret->key_data;
}

const char* lang_bytecode_table_sym_id_to_string(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id, uint32_t n)
{
	if(NULL == table || id.type != LANG_BYTECODE_OPERAND_SYM) ERROR_PTR_RETURN_LOG("Invalid arguments");

	const hashmap_find_res_t* result = VECTOR_GET_CONST(hashmap_find_res_t, table->symbol_table.vector, id.id);
	if(NULL == result) ERROR_PTR_RETURN_LOG("No such entry in the symbol table");

	if(n * sizeof(uint32_t) >= result->key_size) ERROR_PTR_RETURN_LOG("Section index out of boundary");

	uint32_t strid = ((uint32_t*)result->key_data)[n];

	result = VECTOR_GET_CONST(hashmap_find_res_t, table->string_table.vector, strid);
	if(NULL == result) ERROR_PTR_RETURN_LOG("No such string in the string table");

	return (const char*)result->key_data;
}

uint32_t lang_bytecode_table_get_num_regs(const lang_bytecode_table_t* table)
{
	if(NULL == table) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	return table->reg_count;
}

static inline int _operand_to_string_buffer(const lang_bytecode_table_t* table, lang_bytecode_operand_id_t id, string_buffer_t* buffer)
{
	switch(id.type)
	{
		case LANG_BYTECODE_OPERAND_STR:
		case LANG_BYTECODE_OPERAND_GRAPHVIZ:
		{
			const char* rc = lang_bytecode_table_str_id_to_string(table, id);
			if(NULL == rc) ERROR_RETURN_LOG(int, "Cannot read the string operand");

			if(LANG_BYTECODE_OPERAND_GRAPHVIZ != id.type)
			    string_buffer_append("(string)", buffer);
			else
			    string_buffer_append("(graphviz)", buffer);

			string_buffer_appendf(buffer, "\"%s\"", rc);
			break;
		}
		case LANG_BYTECODE_OPERAND_SYM:
		{
			uint32_t size = lang_bytecode_table_sym_id_length(table, id);
			if(ERROR_CODE(uint32_t) == size) ERROR_RETURN_LOG(int, "Cannot read the sym");
			uint32_t i;
			for(i = 0; i < size; i ++)
			{
				const char* sym_str = lang_bytecode_table_sym_id_to_string(table, id, i);
				if(NULL == sym_str)
				{
					LOG_WARNING("cannot find the string for string #%d", id.id);
					sym_str = "<unknown_symbol>";
				}
				if(i == size - 1)
				    string_buffer_append(sym_str, buffer);
				else
				    string_buffer_appendf(buffer, "%s.", sym_str);
			}
			break;
		}
		case LANG_BYTECODE_OPERAND_INT:
		    string_buffer_appendf(buffer, "%d", id.num);
		    break;
		case LANG_BYTECODE_OPERAND_REG:
		    string_buffer_appendf(buffer, "R%d", id.id);
		    break;
		case LANG_BYTECODE_OPERAND_BUILTIN:
		    string_buffer_appendf(buffer, "__builtin_%s", _builtin_str[id.id]);
		    break;
		default: ERROR_RETURN_LOG(int, "Unexpected operand typecode");
	}

	return 0;
}

int lang_bytecode_table_append_to_string_buffer(const lang_bytecode_table_t* table, uint32_t offset, string_buffer_t* buffer)
{
	if(NULL == table || vector_length(table->bytecodes) <= offset || NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");

	int opcode;
	if((opcode = lang_bytecode_table_get_opcode(table, offset)) == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot get the bytecode");
	string_buffer_append(_opcode_str[opcode], buffer);
	uint32_t nops = lang_bytecode_table_get_num_operand(table, offset);

	if(ERROR_CODE(uint32_t) == nops) ERROR_RETURN_LOG(int, "Cannot get the number of operands");

	uint32_t i;
	for(i = 0; i < nops; i ++)
	{
		if(i != 0) string_buffer_append(",", buffer);
		string_buffer_append(" ", buffer);
		lang_bytecode_operand_id_t id;
		if(lang_bytecode_table_get_operand(table, offset, i, &id) == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot fetch the operand");

		if(_operand_to_string_buffer(table, id, buffer) == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot print the operand");
	}

	return 0;
}

uint32_t lang_bytecode_table_get_num_bytecode(const lang_bytecode_table_t* table)
{
	if(NULL == table) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	return (uint32_t)vector_length(table->bytecodes);
}

int lang_bytecode_table_print(const lang_bytecode_table_t* table)
{
	uint32_t n_inst = lang_bytecode_table_get_num_bytecode(table);

	if(ERROR_CODE(uint32_t) == n_inst) ERROR_RETURN_LOG(int, "Cannot get the size of the bytecode table");

	uint32_t i;
	for(i = 0; i < n_inst; i ++)
	{
		char buffer[1024];
		string_buffer_t sb;
		string_buffer_open(buffer, sizeof(buffer), &sb);
		lang_bytecode_table_append_to_string_buffer(table, i, &sb);
		const char* result = string_buffer_close(&sb);
		if(NULL == result)
		{
			LOG_WARNING("cannot close the string buffer");
			return ERROR_CODE(int);
		}

		LOG_INFO("0x%.8x\t%s", i, result);
	}

	return 0;
}


lang_bytecode_label_table_t* lang_bytecode_label_table_new()
{
	lang_bytecode_label_table_t* ret = (lang_bytecode_label_table_t*)calloc(1, sizeof(lang_bytecode_label_table_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the bytecode label table");

	ret->address_table = vector_new(sizeof(uint32_t), LANG_BYTECODE_LABEL_VECTOR_INIT_SIZE);

	if(NULL == ret->address_table)
	{
		free(ret);
		ERROR_PTR_RETURN_LOG("Cannot create new vector for the label table");
		return NULL;
	}

	return ret;
}

int lang_bytecode_label_table_free(lang_bytecode_label_table_t* table)
{
	if(NULL == table) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;
	if(NULL != table->address_table)
	    rc = vector_free(table->address_table);

	free(table);
	return rc;
}

uint32_t lang_bytecode_label_table_new_label(lang_bytecode_label_table_t* table)
{
	if(NULL == table)
	    ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	size_t next_id = vector_length(table->address_table);
	if(ERROR_CODE(size_t) == next_id)
	    ERROR_RETURN_LOG(uint32_t, "Cannot get the next label id");

	uint32_t ret = (uint32_t)next_id;

	uint32_t address = ERROR_CODE(uint32_t);

	vector_t* new_vector = vector_append(table->address_table, &address);
	if(NULL == new_vector)
	    ERROR_RETURN_LOG(uint32_t, "Cannot insert the new label to the label table");

	table->address_table = new_vector;

	LOG_DEBUG("New label L%u has been created", ret);
	return ret;
}

int lang_bytecode_label_table_assign_current_address(lang_bytecode_label_table_t* label_table, const lang_bytecode_table_t* bc_table, uint32_t id)
{
	if(NULL == label_table || NULL == bc_table || id == ERROR_CODE(uint32_t) || id >= vector_length(label_table->address_table))
	    ERROR_RETURN_LOG(int, "Invalid arguments");
	size_t offset = vector_length(bc_table->bytecodes);
	if(ERROR_CODE(size_t) == offset)
	    ERROR_RETURN_LOG(int, "Cannot get the offset from the bytecode table");

	uint32_t address = (uint32_t)offset;

	uint32_t* slot = VECTOR_GET(uint32_t, label_table->address_table, id);
	if(NULL == slot) ERROR_RETURN_LOG(int, "Cannot write the label table");
	*slot = address;

	LOG_DEBUG("Label L%u has been assgiend with address 0x%x", id, address);

	return 0;
}

int lang_bytecode_table_patch(lang_bytecode_table_t* table, const lang_bytecode_label_table_t* labels)
{
	if(NULL == table || NULL == labels) ERROR_RETURN_LOG(int, "Invalid arguments");

	size_t num_bc = lang_bytecode_table_get_num_bytecode(table);
	if(ERROR_CODE(size_t) == num_bc) ERROR_RETURN_LOG(int, "Cannot get the number of the bytecode table");

	size_t i;
	for(i = 0; i < num_bc; i ++)
	{
		_bytecode_t* current_bc = VECTOR_GET(_bytecode_t, table->bytecodes, i);
		if(NULL == current_bc) ERROR_RETURN_LOG(int, "Cannot get the bytecode at address 0x%zx", i);

		uint32_t j;
		for(j = 0; j < sizeof(current_bc->operands) / sizeof(current_bc->operands[0]); j ++)
		{
			if(LANG_BYTECODE_OPERAND_LABEL == current_bc->operand_type[j])
			{
				uint32_t idx = current_bc->operands[j].u;
				if(vector_length(labels->address_table) <= idx)
				    ERROR_RETURN_LOG(int, "Invalid label index %u", idx);
				const uint32_t* addr = VECTOR_GET_CONST(uint32_t, labels->address_table, idx);
				if(NULL == addr)
				    ERROR_RETURN_LOG(int, "Cannot read the label table");
				if(ERROR_CODE(uint32_t) == *addr)
				    ERROR_RETURN_LOG(int, "The address for label L%u is unknown", idx);
				current_bc->operand_type[j] = LANG_BYTECODE_OPERAND_INT;
				current_bc->operands[j].u = *addr;
				LOG_DEBUG("Patched Label L%u -> 0x%x for bytecode 0x%zx, operand %u", idx, *addr, i, j);
			}
		}
	}
	return 0;
}
