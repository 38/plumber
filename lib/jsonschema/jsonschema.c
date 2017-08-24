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

#include <utils/static_assertion.h>

#include <jsonschema.h>
#include <jsonschema/log.h>

/**
 * @brief The type of this schema
 **/
typedef enum {
	_SCHEMA_TYPE_LIST,           /*!< A JSON list */
	_SCHEMA_TYPE_OBJ,            /*!< A JSON object */
	_SCHEMA_TYPE_PRIMITIVE,      /*!< A primitive data, which can be int/float/boolean/string */
	_SCHEMA_TYPE_NTYPES          /*!< The number of types */
} _schema_type_t;

/**
 * @brief The data used to describe the data schema
 **/
typedef enum {
	_INT    = 1,     /*!< The integer primitive */
	_FLOAT  = 2,     /*!< The float  primitive */
	_BOOL   = 4,     /*!< The boolean primitive */
	_STRING = 8      /*!< The string primitive */
} _primitive_t;

/**
 * @brief The schema data for a list
 **/
typedef struct {
	uint32_t       size;      /*!< The size of this schema data */
	uint32_t       repeat:1;  /*!< Indicates if this list contains a start at the end of the schema defnition,
							   *   Which means the list pattern repeats */
	jsonschema_t** element;   /*!< The actual elements */
} _list_t;

/**
 * @brief The object element
 **/
typedef struct {
	char*            key;  /*!< The key of this member */
	jsonschema_t*    val;  /*!< The value of this mebmer */
} _obj_elem_t;

/**
 * @brief The schema data for an object
 **/
typedef struct {
	uint32_t     size;        /*!< The size of this schema */
	_obj_elem_t* element;     /*!< The actual elements */
} _obj_t;

/**
 * @brief The actual definition of the JSON schema object
 **/
struct _jsonschema_t {
	uint32_t              nullable:1;   /*!< Indicates if this schema can be null */
	_schema_type_t        type;         /*!< The type of this */
	uint64_t              __padding__[0];
	_primitive_t          primitive[0]; /*!< The primitive data */
	_obj_t                obj[0];       /*!< The object data */
	_list_t               list[0];      /*!< The list data */
};
STATIC_ASSERTION_LAST(jsonschema_t, primitive);
STATIC_ASSERTION_LAST(jsonschema_t, obj);
STATIC_ASSERTION_LAST(jsonschema_t, list);
STATIC_ASSERTION_SIZE(jsonschema_t, primitive, 0);
STATIC_ASSERTION_SIZE(jsonschema_t, obj, 0);
STATIC_ASSERTION_SIZE(jsonschema_t, list, 0);

/**
 * @brief Dispose the JSON schema
 * @param schema The schema object
 * @return status code
 **/
static inline int _schema_free(jsonschema_t* schema)
{
	int rc = 0;
	uint32_t i;
	switch(schema->type)
	{
		case _SCHEMA_TYPE_LIST:
			if(schema->list->element != NULL)
			{
				for(i = 0; i < schema->list->size; i ++)
					if(NULL != schema->list->element[i] && ERROR_CODE(int) == _schema_free(schema->list->element[i]))
						rc = ERROR_CODE(int);
				free(schema->list->element);
			}
			break;
		case _SCHEMA_TYPE_PRIMITIVE:
			rc = 0;
			break;
		case _SCHEMA_TYPE_OBJ:
			if(schema->obj->element != NULL)
			{
				for(i = 0; i < schema->obj->size; i ++)
				{
					_obj_elem_t* elem = schema->obj->element + i;
					if(NULL != elem->key) free(elem->key);
					if(ERROR_CODE(int) == _schema_free(elem->val))
						rc = ERROR_CODE(int);
				}
				free(schema->obj->element);
			}
			break;
		default:
			rc = ERROR_CODE(int);
			LOG_ERROR("Invalid schema type");
			break;
	}
	free(schema);
	return rc;
}

/**
 * @brief Create new primitive schema from the string description
 * @note  The syntax of the type description should be "[type](|[type])*" 
 *        where type can be either int, float, string or bool or null which 
 *        indicates this value is nullable
 * @param desc The description string
 * @return The newly created schema object, NULL on error
 **/
static inline jsonschema_t* _primitive_new(const char* desc)
{
	static const char _int[]      = "int";
	static const char _float[]    = "float";
	static const char _string[]   = "string";
	static const char _bool[]     = "bool";
	static const char _nullable[] = "nullable";

	_primitive_t types = 0;
	uint32_t nullable = 0;

	for(;*desc;)
	{
		char ch = *desc;
		const char* keyword = NULL;
		size_t      keylen = 0;
		switch(ch)
		{
			case 'i':
				keyword = _int;
				keylen  = sizeof(_int) - 1;
				types |= _INT;
				break;
			case 'f':
				keyword = _float;
				keylen  = sizeof(_float) - 1;
				types |= _FLOAT;
				break;
			case 's':
				keyword = _string;
				keylen  = sizeof(_string) - 1;
				types |= _STRING;
				break;
			case 'b':
				keyword = _bool;
				keylen  = sizeof(_bool) - 1;
				types |= _BOOL;
				break;
			case 'n':
				keyword = _nullable;
				keylen  = sizeof(_nullable) - 1;
				nullable = 1;
				break;
			default:
				/* Simply do nothing */
				(void)0;
		}

		for(;keyword != NULL && keylen > 0 && *keyword == *desc; keyword ++, keylen--, desc ++);
		
		if(NULL == keyword) ERROR_PTR_RETURN_LOG("Invalid type description");

		if(*desc == '|') desc ++;
	}

	jsonschema_t* ret = (jsonschema_t*)malloc(sizeof(jsonschema_t) + sizeof(_primitive_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Canont allocate memory for the primitive schema");

	ret->nullable = (nullable > 0);
	ret->type = _SCHEMA_TYPE_PRIMITIVE;
	ret->primitive[0] = types;

	return ret;
}

/**
 * @brief Create a new object schema from the schema object
 * @param object The schema object
 * @note  This function assume the object
 * @return the newly created schema
 **/
static inline jsonschema_t* _list_new(json_object* object)
{
	size_t len = (size_t)json_object_array_length(object);
	jsonschema_t* ret = (jsonschema_t*)calloc(sizeof(jsonschema_t) + sizeof(_list_t), 1);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new schema object");

	if(NULL == (ret->list->element = (jsonschema_t**)calloc(sizeof(jsonschema_t*) , len)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the element array");

	size_t i;
	for(i = 0; i < len; i ++)
	{
		json_object* curobj = json_object_array_get_idx(object, (int)i);
		if(NULL == curobj) ERROR_LOG_GOTO(ERR, "Cannot get the object[%zu] from JSON object %p", i, object);

		if(i == len - 1 && json_object_is_type(curobj, json_type_string))
		{
			const char* str = json_object_get_string(object);
			if(strcmp(str, "*") == 0) 
			{
				ret->list->size --;
				ret->list->repeat = 1u;
				continue;
			}
		}
		if(NULL == (ret->list->element[i] = jsonschema_new(curobj)))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create the child JSON schmea");
	}

	return ret;
ERR:
	if(NULL != ret)
	{
		if(ret->list->element != NULL)
		{
			if(NULL != ret->list->element) 
			{
				uint32_t i;
				for(i = 0; i < ret->list->size; i ++)
				{
					if(NULL != ret->list->element[i])
						_schema_free(ret->list->element[i]);
				}
			}
			free(ret->list->element);
		}
		free(ret);
	}
	return NULL;
}

/**
 * @brief Create new object based on the given schema object
 * @param obj The schema object
 * @note This function handles the key __schema_property__ differently
 * @return The newly created object
 **/
static inline jsonschema_t* _obj_new(json_object* obj)
{
	const char* _property_key = "__schema_property__";

	json_object* prop;
	uint32_t nullable = 0;
	if(json_object_object_get_ex(obj, _property_key, &prop))
	{
		/* Indicates we have an object */
		if(!json_object_is_type(prop, json_type_string))
			ERROR_PTR_RETURN_LOG("The JSON property must be a string");

		/* Then we need to parse the string */
		const char* str = json_object_get_string(prop);
		if(strcmp(str, "nullable") == 0) 
			nullable = 1;
		else 
			ERROR_PTR_RETURN_LOG_ERRNO("Invalid schema property");
	}

	jsonschema_t* ret = (jsonschema_t*)calloc(sizeof(jsonschema_t) + sizeof(_obj_t), 1);
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new JSON schema");

	ret->nullable = (nullable > 0);

	size_t len = (size_t)(json_object_object_length(obj) - (int)nullable);

	if(NULL == (ret->obj->element = (_obj_elem_t*)calloc(sizeof(_obj_elem_t), len)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the member array");
	ret->obj->size = (uint32_t)len;

	json_object_iter iter;
	uint32_t cnt = 0;
	json_object_object_foreachC(obj, iter)
	{
		if(nullable && strcmp(iter.key, _property_key) == 0) continue;

		_obj_elem_t* this = ret->obj->element + (cnt ++);

		if(NULL == (this->key = strdup(iter.key)))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the key name");

		if(NULL == (this->val = jsonschema_new(iter.val)))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create the member schema");
	}

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->obj->element)
		{
			uint32_t i;
			for(i = 0; i < ret->obj->size; i ++)
			{
				if(NULL != ret->obj->element[i].key)
					free(ret->obj->element[i].key);
				if(NULL != ret->obj->element[i].val)
					_schema_free(ret->obj->element[i].val);
			}
			free(ret->obj->element);
		}

		free(ret);
	}
	return NULL;
}

jsonschema_t* jsonschema_new(json_object* schema_obj)
{
	if(NULL == schema_obj)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	int type = json_object_get_type(schema_obj);

	switch(type)
	{
		case json_type_object:
			return _obj_new(schema_obj);
		case json_type_array:
			return _list_new(schema_obj);
		case json_type_string:
		{
			const char* str = json_object_get_string(schema_obj);
			if(NULL == str) ERROR_PTR_RETURN_LOG("Cannot get the underlying string");
			return _primitive_new(str);
		}
		default:
			ERROR_PTR_RETURN_LOG("Invalid schema data type");
	}
}

int jsonschema_free(jsonschema_t* schema)
{
	if(NULL == schema)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	return _schema_free(schema);
}
