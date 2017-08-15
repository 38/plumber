/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include <json.h>

#include <error.h>

#include <jsonschema.h>
#include <jsonschema/log.h>

/**
 * @brief The type of the schema element
 **/
typedef enum {
	_ELEMTYPE_OPEN_OBJ,   /*!< The element that used to open an object */
	_ELEMTYPE_CLOSE_OBJ,  /*!< The element that close an object */
	_ELEMTYPE_OPEN_LIST,  /*!< Open an list */
	_ELEMTYPE_CLOSE_LIST, /*!< Close a list */
	_ELEMTYPE_PRIMITIVE,  /*!< The primitive type */
	_ELEMTYPE_REPEAT      /*!< The repeat instruction, which is used in a list */
} _elemtype_t;

/**
 * @brief The type descriptor in a type
 **/
typedef enum {
	_PRIMITIVE_TYPE_INTEGER = 1,   /*!< The number type */
	_PRIMITIVE_TYPE_FLOAT   = 2,   /*!< The float point number */
	_PRIMITIVE_TYPE_STRING  = 4,   /*!< The string type */
	_PRIMITIVE_TYPE_NULL    = 8,   /*!< This is a primitive */
	_PRIMITIVE_TYPE_BOOLEAN = 16  /*!< This is a boolean type */
} _primitive_type_t;

typedef union {
	char*     key;   /*!< The key in the object, this is only valid when current opened object is a dict */
	uint32_t  idx;   /*!< The index in an array, this is only valid when current opened object is a list */
} _address_t;

/**
 * @brief The actual schema element 
 **/
typedef struct {
	_elemtype_t type;  /*!< The element type */
	_address_t  addr;  /*!< How we locate this element */
	_primitive_type_t primitive;   /*!< This field only valid when the element is an primitve */
} _element_t;

/**
 * @brief The actual data struture for a JSON schema 
 **/
struct _jsonschema_t {
	uint32_t    capacity;   /*!< The capacity of the schema element */
	uint32_t    count;      /*!< The number of the schema element current in the JSON schema object */
	_element_t* elem;       /*!< The element list */
};

/**
 * @brief ensure the schema element array have enough space
 * @param buf the schema buffer
 * @return status code
 **/
static inline int _schema_ensure_space(jsonschema_t* buf)
{
	if(buf->capacity < buf->count + 1)
	{
		_element_t* new_arr = realloc(buf->elem, buf->capacity * 2 * sizeof(_element_t));
		if(NULL == new_arr) ERROR_RETURN_LOG(int, "Cannot resize the schema element array");
		buf->capacity *= 2;
		buf->elem = new_arr;
	}
	return 0;
}

/**
 * @brief append a primitive to the schema element array
 * @param type The type of this primitive should be accepted
 * @param addr The address 
 * @param buf  The schema buffer
 * @return status code
 **/
static inline int _schema_append_primitive(_primitive_type_t type, _address_t addr, jsonschema_t* buf)
{
	if(ERROR_CODE(int) == _schema_ensure_space(buf))
		ERROR_RETURN_LOG(int, "Cannot make sure the schema object contains enough memory");

	_element_t* this = buf->elem + buf->count;
	this->type = _ELEMTYPE_PRIMITIVE;
	this->addr = addr;
	this->primitive = type;

	buf->count ++;

	return 0;
}

/**
 * @brief Append a repeat directive to the schema object
 * @param addr The object address
 * @param buf  The schema buffer
 * @return status code
 **/
static inline int _schema_append_repeat(_address_t addr, jsonschema_t* buf)
{
	if(ERROR_CODE(int) == _schema_ensure_space(buf))
		ERROR_RETURN_LOG(int, "Cannot make sure the schema object contains enough memory");

	_element_t* this = buf->elem + buf->count;
	this->type = _ELEMTYPE_REPEAT;
	this->addr = addr;
	this->primitive = 0;

	buf->count ++;

	return 0;
}

/**
 * @biref Append the object operation
 * @param is_obj indicates if this is an object, if this options is 0, means we have an array
 * @param is_open indicates if we want to open the object, otherwise we are closing the object
 * @param addr The addrewss
 * @param buf The schema object buffer
 * @rreturn status code
 **/
static inline int _schema_append_objops(int is_obj, int is_open, _address_t addr, jsonschema_t* buf)
{
	if(ERROR_CODE(int) == _schema_ensure_space(buf))
		ERROR_RETURN_LOG(int, "Cannot make sure the schema object contains enough memory");

	_element_t* this = buf->elem + buf->count;

	if(is_obj)
		this->type = is_open ? _ELEMTYPE_OPEN_OBJ : _ELEMTYPE_CLOSE_OBJ;
	else 
		this->type = is_open ? _ELEMTYPE_OPEN_LIST : _ELEMTYPE_CLOSE_LIST;

	this->addr = addr;
	this->primitive = 0;

	buf->count ++;

	return 0;
}

/**
 * @brief parse a type name
 * @param ptr_str The pointer to the string to parse
 * @return The type code we parsed from the string
 **/
static inline _primitive_type_t _parse_primitive_type(char const* * ptr_str)
{
	const char* ptr = *ptr_str;
	const char* expected = NULL;
	_primitive_type_t ret = 0;
	switch(*ptr)
	{
		case 'i': 
			expected = "int";
			ret = _PRIMITIVE_TYPE_INTEGER;
			break;
		case 'f':
			expected = "float";
			ret = _PRIMITIVE_TYPE_FLOAT;
			break;
		case 's':
			expected = "string";
			ret = _PRIMITIVE_TYPE_STRING;
			break;
		case 'n':
			expected = "null";
			ret = _PRIMITIVE_TYPE_NULL;
			break;
		case 'b':
			expected = "bool";
			ret = _PRIMITIVE_TYPE_BOOLEAN;
			break;
		default:
			break;
	}
	if(expected == NULL) return 0;
	for(;*expected && *expected == *ptr; expected ++, ptr ++);
	if(*expected == 0) return ret;
	return 0;
}

/**
 * @brief process a primitive schema 
 * @param type_cons The type constrain we want to enforce
 * @param addr      The address
 * @arapm buf       The schema buffer
 * @return status code
 **/
static inline _primitive_type_t _process_primitive_schema(const char* type_cons, _address_t addr, jsonschema_t* buf)
{
	_primitive_type_t type = 0;
	for(;*type_cons;)
	{
		_primitive_type_t this_type = _parse_primitive_type(&type_cons);
		if(0 == this_type) ERROR_RETURN_LOG(int, "Cannot parse the type constain");
		type |= this_type;
		if(*type_cons == '|') type_cons ++;
	}

	if(ERROR_CODE(int) == _schema_append_primitive(type, addr, buf))
		ERROR_RETURN_LOG(int, "Cannot append the primitive description");
	return type;
}

/**
 * @brief Traverse the schema object and convert it to a schema data structure
 * @param schema_obj The schema object we are dealing with
 * @param addr The address for this schema_object
 * @param buf the output buffer
 * @return status code
 **/
static inline int _traverse_schema(json_object* schema_obj, _address_t addr, jsonschema_t* buf)
{
	enum json_type json_type = json_object_get_type(schema_obj);

	switch(json_type)
	{
		case json_type_string:
		{
			/* A string can be either a wildcard, or a type descriptor */
			const char* str = json_object_get_string(schema_obj);
			if(str[0] == '*')
			{
				if(str[1] != 0)
					ERROR_RETURN_LOG(int, "Invalid wildcard, it should be exactly \"*\"");
				if(ERROR_CODE(int) == _schema_append_repeat(addr, buf))
					ERROR_RETURN_LOG(int, "Cannot append the repeat directive to the schema object");
				return 0;
			}
			else return _process_primitive_schema(str, addr, buf);
		}
		case json_type_array:
		{
			/* The array */
			int size = (size_t)json_object_array_length(schema_obj);
			if(ERROR_CODE(int) == _schema_append_objops(/* is_obj */ 0, /* is_open */ 1, addr, buf))
				ERROR_RETURN_LOG(int, "Cannot create open directive for the list operation");

			int i;
			for(i = 0; i < size; i ++)
			{
				json_object* member = json_object_array_get_idx(schema_obj, i);
				_address_t member_addr = {
					.idx = (uint32_t)i
				};
				if(ERROR_CODE(int) == _traverse_schema(member, member_addr, buf))
					ERROR_RETURN_LOG(int, "Cannot traverse list member %d", i);
			}

			if(ERROR_CODE(int) == _schema_append_objops(/* is_obj */ 0, /* is_open */ 0, addr, buf))
				ERROR_RETURN_LOG(int, "Cannot create the close directive for the list operation");

			return 0;
		}
		case json_type_object:
		{
			if(ERROR_CODE(int) == _schema_append_objops(/* is_obj */ 1, /* is_open */ 1, addr, buf))
				ERROR_RETURN_LOG(int, "Cannot create open directive for the object operation");
			json_object_iter iter;
			
			json_object_object_foreachC(schema_obj, iter)
			{
				char* key = strdup(iter.key);
				json_object* obj = iter.val;
				if(NULL == key)
					ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the key in the object");
				_address_t member_addr = {
					.key = key
				};
				if(ERROR_CODE(int) == _traverse_schema(obj, member_addr, buf))
				{
					if(NULL != key) free(key);
					ERROR_RETURN_LOG(int, "Error when traversing the member object");
				}
			}
			if(ERROR_CODE(int) == _schema_append_objops(/* is_obj */ 1, /* is_open */ 0, addr, buf))
				ERROR_RETURN_LOG(int, "Cannot create close directive for the object operation");
			return 0;
		}
		default:
			ERROR_RETURN_LOG(int, "Invalid schema data type");
	}
}

int jsonschema_free(jsonschema_t* schema)
{
	if(NULL == schema) ERROR_RETURN_LOG(int, "Invalid arguments");
	uint64_t stack[(schema->count + 63) / 64];
	uint32_t sp = 0, i;
	for(i = 0; i < schema->count; i ++)
	{
		_element_t* this = schema->elem + i;
		if(sp > 0 && (stack[(sp - 1) / 64] & (1ull << ((sp - 1) % 64))) > 0)
			free(this->addr.key);
		switch(this->type)
		{
			case _ELEMTYPE_OPEN_OBJ:
			case _ELEMTYPE_OPEN_LIST:
				stack[sp/64] |= (((uint64_t)(this->type == _ELEMTYPE_OPEN_OBJ)) << (sp %64));
				sp ++;
				break;
			case _ELEMTYPE_CLOSE_LIST:
			case _ELEMTYPE_CLOSE_OBJ:
				sp --;
			default:
				break;
		}
	}
	free(schema->elem);
	free(schema);
	return 0;
}

jsonschema_t* jsonschema_new(json_object* schema_obj)
{
	if(NULL == schema_obj)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	_address_t nil = {};
	jsonschema_t* ret = (jsonschema_t*)calloc(1, sizeof(jsonschema_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the json schema object");

	if(ERROR_CODE(int) == _traverse_schema(schema_obj, nil, ret))
		ERROR_LOG_GOTO(ERR, "Traversing schema object");

	return ret;
ERR:
	if(NULL != ret) jsonschema_free(ret);
	return NULL;
}

jsonschema_t* jsonschema_from_string(const char* schema_str)
{
	if(NULL == schema_str) ERROR_PTR_RETURN_LOG("Invalid arguments");

	json_object* obj = json_tokener_parse(schema_str);
	if(NULL == obj) ERROR_PTR_RETURN_LOG("Cannot parse the JSON schema string, is the string a valid JSON?");

	jsonschema_t* ret = jsonschema_new(obj);

	if(NULL == ret) 
	{
		json_object_put(obj);
		ERROR_PTR_RETURN_LOG("Cannot create JSON schema from the input JSON object");
	}

	json_object_put(obj);
	return ret;
}

jsonschema_t* jsonschema_from_file(const char* schema_file)
{
	if(NULL == schema_file)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	struct stat st;
	if(stat(schema_file, &st) < 0)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot access the stat of the schema file %s", schema_file);


	size_t sz = (size_t)st.st_size;

	if(sz == 0) ERROR_PTR_RETURN_LOG("Empty file as schema is not allowed");
	char* buf = (char*)malloc(sz + 1);
	FILE* fp = NULL;
	if(NULL == buf) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the schema file content");
	buf[sz] = 0;
	if(NULL == (fp = fopen(schema_file, "r")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open file %s", schema_file);

	if(1 != fread(buf, sz, 1, fp))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read data form file %s", schema_file);

	jsonschema_t* ret = jsonschema_from_string(buf);

	if(NULL == ret) ERROR_LOG_GOTO(ERR, "Cannot create schema from the JSON string");

	free(buf);
	fclose(fp);
	return ret;
ERR:
	if(NULL != ret) free(ret);
	if(NULL != fp) fclose(fp);
	return NULL;
}
