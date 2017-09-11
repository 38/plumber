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

#if 0
-       _INT    = 1,     /*!< The integer primitive */
-       _FLOAT  = 2,     /*!< The float  primitive */
-       _BOOL   = 4,     /*!< The boolean primitive */
-       _STRING = 8      /*!< The string primitive */
#endif

/**
 * @brief The data used to describe the data schema
 **/
typedef struct {
	struct {
		uint32_t allowed:1;     /*!< If integer is allowed */
		int32_t  min;          /*!< The min value of this int */
		int64_t  max;          /*!< The max value of this int */
	}            int_schema;   /*!< The integer constraints */
	struct {
		uint32_t allowed:1;    /*!< If float number is allowed */
		double   min;          /*!< The lower bound of this float number */
		double   max;          /*!< The upper bound of this float number */
	}            float_schema; /*!< The float schema */
	struct {
		uint32_t allowed:1;    /*!< If the boolean value is allowed */
		/* We don't have any additional constraints for boolean, because 
		 * If we only allow it's true, or false, the field will be meaning 
		 * less */
	}            bool_schema;  /*!< The boolean schema */
	struct {
		uint32_t allowed:1;    /*!< If string is allowed */
		size_t   min_len;      /*!< The min length of the string */
		size_t   max_len;      /*!< The max length of the string */
	}            string_schema;/*!< The string scehma */
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
	static const char _nullable[] = "null";

	_primitive_t types = {};
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
				types.int_schema.allowed = 1;
			    break;
			case 'f':
			    keyword = _float;
			    keylen  = sizeof(_float) - 1;
				types.float_schema.allowed = 1;
			    break;
			case 's':
			    keyword = _string;
			    keylen  = sizeof(_string) - 1;
				types.string_schema.allowed = 1;
			    break;
			case 'b':
			    keyword = _bool;
			    keylen  = sizeof(_bool) - 1;
				types.bool_schema.allowed = 1;
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

		if(NULL == keyword || *keyword != 0) ERROR_PTR_RETURN_LOG("Invalid type description");

		/* TODO: parse the additional constraint */

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

	ret->type = _SCHEMA_TYPE_LIST;
	ret->list->size = (uint32_t)len;

	size_t i;
	for(i = 0; i < len; i ++)
	{
		json_object* curobj = json_object_array_get_idx(object, (int)i);
		if(NULL == curobj) ERROR_LOG_GOTO(ERR, "Cannot get the object[%zu] from JSON object %p", i, object);

		if(i == len - 1 && json_object_is_type(curobj, json_type_string))
		{
			const char* str = json_object_get_string(curobj);
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
	ret->type = _SCHEMA_TYPE_OBJ;

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

jsonschema_t* jsonschema_from_string(const char* schema_str)
{
	if(NULL == schema_str) ERROR_PTR_RETURN_LOG("Invalid arguments");

	json_object* schema = json_tokener_parse(schema_str);
	if(NULL == schema) ERROR_PTR_RETURN_LOG("Invalid JSON object");

	jsonschema_t* ret = jsonschema_new(schema);

	json_object_put(schema);
	return ret;
}

jsonschema_t* jsonschema_from_file(const char* schema_file)
{
	jsonschema_t* ret = NULL;
	if(NULL == schema_file) ERROR_PTR_RETURN_LOG("Invalid arguments");

	struct stat st;
	if(stat(schema_file, &st) < 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("stat error");

	FILE* fp = fopen(schema_file, "r");
	if(NULL == fp) ERROR_PTR_RETURN_LOG_ERRNO("Cannot open schema file");

	size_t sz = (size_t)st.st_size;
	char* buffer = (char*)malloc(sz + 1);
	if(NULL == buffer) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the file content");

	buffer[sz] = 0;
	if(1 != fread(buffer, sz, 1, fp))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read data from the schema file");

	ret = jsonschema_from_string(buffer);

ERR:
	if(NULL != buffer) free(buffer);
	if(NULL != fp) fclose(fp);

	return ret;
}

/**
 * @brief Validate a primitive schema element
 * @param data The primitive schema data
 * @param nullable If this object can be nullable
 * @param object The data object
 * @return The vlaidation result, or error code
 **/
static inline int _validate_primitive(const _primitive_t* data, uint32_t nullable, json_object* object)
{
	int type = json_object_get_type(object);
	switch(type)
	{
		case json_type_int:
		{
			int32_t value = json_object_get_int(object);
			return (data->int_schema.allowed && data->int_schema.min <= value && value <= data->int_schema.max) ||
				   (data->float_schema.allowed && data->float_schema.min <= value && value <= data->float_schema.max);
		}
		case json_type_double:
		{
			double value = json_object_get_double(object);
			return (data->float_schema.allowed && data->float_schema.min <= value && value <= data->float_schema.max);
		}
		case json_type_boolean:
			return data->bool_schema.allowed;
		case json_type_string:
		{
			if(data->string_schema.allowed == 0) return 0;
			if(data->string_schema.min_len != 0 || data->string_schema.max_len != (size_t)-1)
			{
				const char* str = json_object_get_string(object);
				if(NULL == str) ERROR_RETURN_LOG(int, "Cannot get the string from JSON string object");
				size_t len = strlen(str);
				return data->string_schema.min_len <= len && len <= data->string_schema.max_len;
			}
			return 1;
		}
		case json_type_null:
		    return nullable > 0;
		default:
		    return 0;
	}
}

/**
 * @brief Validate if the data object is the instance of the given list schema
 * @param data The list schema data
 * @param nullable if this object is nullable
 * @param object The data object
 * @todo currently we don't support nullable list, so we need support that at some point
 * @return status code
 **/
static inline int _validate_list(const _list_t* data, uint32_t nullable, json_object* object)
{
	(void)nullable;
	int is_array = json_object_is_type(object, json_type_array);
	if(!is_array) return is_array;
	uint32_t first = 1;
	uint32_t idx = 0;
	uint32_t len = (uint32_t)json_object_array_length(object);
	for(;first || data->repeat; first = 0)
	{
		uint32_t i;
		for(i = 0; i < data->size && idx < len; i ++, idx ++)
		{
			json_object* elem_data = json_object_array_get_idx(object, (int)idx);
			int rc = jsonschema_validate(data->element[i], elem_data);
			if(ERROR_CODE(int) == rc) return ERROR_CODE(int);
			if(0 == rc) return 0;
		}

		/* It could be zero length */
		if(i == 0 && data->repeat) return 1;

		/* Because we don't fully match the pattern */
		if(i != data->size) return 0;
	}

	return 1;
}

/**
 * @brief Validate if the data object is the instance of the given object schema
 * @param data The schema data
 * @param nullable If this object is a nullable one
 * @param object The data object
 * @return status code
 **/
static inline int _validate_obj(const _obj_t* data, uint32_t nullable, json_object* object)
{
	int type = json_object_get_type(object);
	if(type == json_type_null && nullable) return 1;
	if(type != json_type_object) return 0;

	uint32_t i;
	for(i = 0; i < data->size; i ++)
	{
		json_object* this;
		int rc = json_object_object_get_ex(object, data->element[i].key, &this);

		if(rc == 0) this = NULL;

		int child_rc = jsonschema_validate(data->element[i].val, this);
		if(ERROR_CODE(int) == child_rc)
		    ERROR_RETURN_LOG(int, "Child validation failure");

		if(child_rc == 0) return 0;
	}

	return 1;
}

int jsonschema_validate(const jsonschema_t* schema, json_object* object)
{
	if(NULL == schema) ERROR_RETURN_LOG(int, "Invalid arguments");

	switch(schema->type)
	{
		case _SCHEMA_TYPE_PRIMITIVE:
		    return _validate_primitive(schema->primitive, schema->nullable, object);
		case _SCHEMA_TYPE_LIST:
		    return _validate_list(schema->list, schema->nullable, object);
		case _SCHEMA_TYPE_OBJ:
		    return _validate_obj(schema->obj, schema->nullable, object);
		default:
		    ERROR_RETURN_LOG(int, "Code bug: Invalid schema type");
	}
}

int jsonchema_update(const jsonschema_t* schema, json_object* target, const json_object* patch)
{
	/* TODO: update this not supported currently */
	(void)schema;
	(void)target;
	(void)patch;
	return 0;
}
