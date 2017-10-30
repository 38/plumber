/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

#include <error.h>

#include <rapidjson/filereadstream.h>
#include <rapidjson/writer.h>
#include <rapidjson/memorybuffer.h>

#include <utils/static_assertion.h>

#include <jsonschema.h>
#include <jsonschema/log.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

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
typedef struct {
	struct {
		uint32_t allowed:1;     /*!< If integer is allowed */
		int32_t  min;          /*!< The min value of this int */
		int64_t  max;          /*!< The max value of this int */
	}            int_schema;   /*!< The integer constraints */
	struct {
		uint32_t allowed:1;    /*!< If float number is allowed */
		uint32_t unlimited:1;  /*!< If this float number is unlimited */
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
 * @brief The memory default null object
 **/
static char _null_obj_buf[sizeof(rapidjson::Value)];

/**
 * @brief The default null object
 **/
static rapidjson::Value& _null_obj = *(new (_null_obj_buf)rapidjson::Value());

/* The previous dedcls */
static inline jsonschema_t* _jsonschema_new(const rapidjson::Value& schema_obj);

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
 * @brief The helper function we used to consume all the whitespace after
 *        the start point
 * @param start The start of the string
 * @return The string pointer after we strip the leading white spaces
 **/
static const char* _strip_ws(const char* start)
{
	for(;*start && (*start == '\n' || *start == '\t'); start ++);
	return start;
}

/**
 * @param parse the int constraint in a JSON schema
 * @param types The primitive type data to fill up
 * @param desc The constraint description start location
 * @note The constraint should be either empty or (upper, lower)
 * @return How many bytes has been parsed, or error code
 **/
static long _int_constraint(_primitive_t* types, const char* desc)
{
	const char* begin = desc;
	/* If this is not start with parentheses, means we have empty constraint */
	if(desc[0] != '(')
	{
		types->int_schema.max = INT_MAX;
		types->int_schema.min = INT_MIN;
		return 0;
	}

	/* Otherwise, we need to strip the white space after parentheses */
	desc = _strip_ws(desc + 1);
	types->int_schema.min = (int32_t)strtol(desc, (char**)&desc, 0);

	/* We are expecting a comma otherwise it must be an error */
	desc = _strip_ws(desc);
	if(*desc != ',') ERROR_RETURN_LOG(long, "Invalid int constraint");

	/* Parse the lower bound */
	desc = _strip_ws(desc + 1);
	types->int_schema.max = (int32_t)strtol(desc, (char**)&desc, 0);

	/* Strip the white space and check if we reached the end of the constraint */
	desc = _strip_ws(desc);
	if(desc[0] != ')')
	    ERROR_RETURN_LOG(long, "Invalid int constraint");

	return desc + 1 - begin;
}

/**
 * @param parse the float pointer number constraint in a JSON schema
 * @param types The primitive type data to fill up
 * @param desc The constraint description start location
 * @note The constraint should be either empty or (upper, lower)
 * @return How many bytes has been parsed, or error code
 **/
static long _float_constraint(_primitive_t* types, const char* desc)
{
	const char* begin = desc;

	/* If the constraint is empty, this means we don't have any range limit */
	if(desc[0] != '(')
	{
		types->float_schema.unlimited = 1;
		return 0;
	}

	/* Parse the lower bound of the schema */
	desc = _strip_ws(desc + 1);
	types->float_schema.min = strtod(desc, (char**)&desc);

	/* Check if we have the comma */
	desc = _strip_ws(desc);
	if(*desc != ',') ERROR_RETURN_LOG(long, "Invalid float constraint");

	/* Parse the upper bound */
	desc = _strip_ws(desc + 1);
	types->float_schema.max = strtod(desc, (char**)&desc);

	/* We are expecting the end of the constraint */
	desc = _strip_ws(desc);
	if(desc[0] != ')')
	    ERROR_RETURN_LOG(long, "Invalid float constraint");

	return desc + 1 - begin;
}
/**
 * @brief Parse the string constraint
 * @note A string constraint can be ether no constraint or the range of
 *       the length of the string
 * @param types The type constraint data
 * @param desc The constaint description
 * @return The number of bytes has been parsed or error code
 **/
static long _string_constraint(_primitive_t* types, const char* desc)
{
	const char* begin = desc;
	if(desc[0] != '(')
	{
		types->string_schema.max_len = SIZE_MAX;
		types->string_schema.min_len = 0;
		return 0;
	}

	/* We should parse the lower bound of the string size */
	desc = _strip_ws(desc + 1);
	types->string_schema.min_len = (size_t)strtoll(desc, (char**)&desc, 0);

	/* Then we check if we have the comma between lower and upper bound */
	desc = _strip_ws(desc);
	if(*desc != ',') ERROR_RETURN_LOG(int, "Invalid string constraint");

	/* Finally, we need to parse the upper bound of the string length */
	desc = _strip_ws(desc + 1);
	types->string_schema.max_len = (size_t)strtoll(desc, (char**)&desc, 0);

	/* The last thing, we need to make sure the parentheses is closed */
	desc = _strip_ws(desc);
	if(desc[0] != ')')
	    ERROR_RETURN_LOG(int, "Invalid string constraint");

	return desc + 1 - begin;
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

		/* How we parse the constraint */
		long (*parse_constraint)(_primitive_t*, const char*) = NULL;

		switch(ch)
		{
			case 'i':
			    keyword = _int;
			    keylen  = sizeof(_int) - 1;
			    types.int_schema.allowed = 1;
			    parse_constraint = _int_constraint;
			    break;
			case 'f':
			    keyword = _float;
			    keylen  = sizeof(_float) - 1;
			    types.float_schema.allowed = 1;
			    parse_constraint = _float_constraint;
			    break;
			case 's':
			    keyword = _string;
			    keylen  = sizeof(_string) - 1;
			    types.string_schema.allowed = 1;
			    parse_constraint = _string_constraint;
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

		long cons_len = 0;

		if(NULL != parse_constraint && ERROR_CODE(int) == (cons_len = parse_constraint(&types, desc)))
		    ERROR_PTR_RETURN_LOG("Invalid type constraint");

		desc += cons_len;

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
static inline jsonschema_t* _list_new(const rapidjson::Value::ConstArray& object)
{
	size_t len = object.Size();
	jsonschema_t* ret = (jsonschema_t*)calloc(sizeof(jsonschema_t) + sizeof(_list_t), 1);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new schema object");

	if(NULL == (ret->list->element = (jsonschema_t**)calloc(sizeof(jsonschema_t*) , len)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the element array");

	ret->type = _SCHEMA_TYPE_LIST;
	ret->list->size = (uint32_t)len;

	size_t i;
	for(i = 0; i < len; i ++)
	{
		const rapidjson::Value& curobj = object[(unsigned)i];

		if(i == len - 1 && curobj.IsString() && strcmp(curobj.GetString(), "*") == 0)
		{
			ret->list->size --;
			ret->list->repeat = 1u;
			continue;
		}
		if(NULL == (ret->list->element[i] = _jsonschema_new(curobj)))
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
static inline jsonschema_t* _obj_new(const rapidjson::Value::ConstObject& obj)
{
	static const char* _property_key = JSONSCHEMA_SCHEMA_PROPERTY_KEYNAME;

	uint32_t nullable = (obj.HasMember(_property_key) && obj[_property_key].IsString() && strcmp(obj[_property_key].GetString(), "nullable") == 0);

	jsonschema_t* ret = (jsonschema_t*)calloc(sizeof(jsonschema_t) + sizeof(_obj_t), 1);
	uint32_t cnt = 0;

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new JSON schema");

	ret->nullable = (nullable > 0);

	size_t len = obj.MemberCount() - (size_t)nullable;

	if(NULL == (ret->obj->element = (_obj_elem_t*)calloc(sizeof(_obj_elem_t), len)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the member array");
	ret->obj->size = (uint32_t)len;
	ret->type = _SCHEMA_TYPE_OBJ;

	for(rapidjson::Value::ConstObject::MemberIterator it = obj.MemberBegin();
	    it != obj.MemberEnd(); it ++)
	{
		if(nullable && strcmp(it->name.GetString(), _property_key) == 0) continue;

		_obj_elem_t* this_elem = ret->obj->element + (cnt ++);

		if(NULL == (this_elem->key = strdup(it->name.GetString())))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the key name");

		if(NULL == (this_elem->val = _jsonschema_new(it->value)))
		    ERROR_LOG_GOTO(ERR, "Cannot create the member schema");
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

/**
 * @brief Validate a primitive schema element
 * @param data The primitive schema data
 * @param nullable If this object can be nullable
 * @param object The data object
 * @return The vlaidation result, or error code
 **/
static inline int _validate_primitive(const _primitive_t* data, uint32_t nullable, const rapidjson::Value& object)
{
	switch(object.GetType())
	{
		case rapidjson::kNumberType:
		    if(object.IsInt64())
		    {
			    int64_t value = object.GetInt64();
			    return (data->int_schema.allowed && data->int_schema.min <= value && value <= data->int_schema.max) ||
			           (data->float_schema.allowed && data->float_schema.min <= value && value <= data->float_schema.max);
		    }
		    else if(object.IsDouble())
		    {
			    double value = object.GetDouble();
			    return (data->float_schema.allowed && data->float_schema.min <= value && value <= data->float_schema.max);
		    }
		    else return 0;
		case rapidjson::kTrueType:
		case rapidjson::kFalseType:
		    /* Boolean schema don't have any constraint, because
		     * the only constraint should be "this must be true" or
		     * "this must be false", if this is the case, why we still
		     * need this field */
		    return data->bool_schema.allowed;
		case rapidjson::kStringType:
		{
			if(data->string_schema.allowed == 0) return 0;
			if(data->string_schema.min_len != 0 ||
			   data->string_schema.max_len != SIZE_MAX)
			{
				const char* str = object.GetString();
				if(NULL == str) ERROR_RETURN_LOG(int, "Cannot get the string from JSON string object");
				size_t len = strlen(str);
				return data->string_schema.min_len <= len &&
				       len <= data->string_schema.max_len;
			}
			return 1;
		}
		case rapidjson::kNullType:
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
static inline int _validate_list(const _list_t* data, uint32_t nullable, const rapidjson::Value& object)
{
	(void)nullable;
	if(!object.IsArray()) return 0;

	const rapidjson::Value::ConstArray& arr = object.GetArray();

	uint32_t first = 1;
	uint32_t idx = 0;
	uint32_t len = arr.Size();
	for(;first || data->repeat; first = 0)
	{
		uint32_t i;
		for(i = first ? 0 : data->size - 1; i < data->size && idx < len; i ++, idx ++)
		{
			const rapidjson::Value& elem_data = arr[idx];
			int rc = jsonschema_validate_obj(data->element[i], elem_data);
			if(ERROR_CODE(int) == rc) return ERROR_CODE(int);
			if(0 == rc)
			{
				LOG_DEBUG("List item validation failed: %u", i);
				return 0;
			}
		}

		/* It could be zero length */
		if(i == 0 && data->repeat)
		    return 1;

		/* Because we don't fully match the pattern */
		if(i != data->size)
		    return 0;
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
static inline int _validate_obj(const _obj_t* data, uint32_t nullable, const rapidjson::Value& object)
{
	if(nullable && object.IsNull()) return 1;
	if(!object.IsObject()) return 0;

	uint32_t i;
	for(i = 0; i < data->size; i ++)
	{
		const rapidjson::Value& this_obj = object.HasMember(data->element[i].key) ? object[data->element[i].key] : _null_obj;

		int child_rc = jsonschema_validate_obj(data->element[i].val, this_obj);

		if(ERROR_CODE(int) == child_rc)
		    ERROR_RETURN_LOG(int, "Child validation failure");

		if(child_rc == 0)
		{
			LOG_DEBUG("Dictionary validation failed: %s", data->element[i].key);
			return 0;
		}
	}

	return 1;
}

/**
 * @brief Create a new schema object from the Rapid JSON object
 * @param schema_obj The schema object
 * @return The schema object
 **/
static jsonschema_t* _jsonschema_new(const rapidjson::Value& schema_obj)
{
	switch(schema_obj.GetType())
	{
		case rapidjson::kObjectType:
		    return _obj_new(schema_obj.GetObject());
		case rapidjson::kArrayType:
		    return _list_new(schema_obj.GetArray());
		case rapidjson::kStringType:
		{
			const char* str = schema_obj.GetString();
			if(NULL == str) ERROR_PTR_RETURN_LOG("Cannot get the underlying string");
			return _primitive_new(str);
		}
		default:
		    ERROR_PTR_RETURN_LOG("Invalid schema data type");
	}
}

/**
 * @brief Patch the target value with the given patch
 * @param schema Schema
 * @param nullable If this value is nullable
 * @param target The target value
 * @param patch The patched value
 * @return status code
 **/
static int _patch_primitive(const _primitive_t* schema, rapidjson::Value& target, rapidjson::Value& patch)
{
	if(1 != _validate_primitive(schema, 0, patch))
	    ERROR_RETURN_LOG(int, "Invalid primitive value");
	target = patch;
	return 0;
}

/**
 * @brief Patch a given list
 * @param schema The schema
 * @param nullable if this is nullable
 * @param target The target object
 * @param patch The patch object
 * @return status code
 **/
static int _patch_list(const _list_t* schema, rapidjson::Value& target, rapidjson::Value& patch, rapidjson::Document::AllocatorType& allocator)
{
	rapidjson::Value::Array target_arr = target.GetArray();
	if(patch.IsArray())
	{
		/* We need to patch the entire array */
		target = patch;
		return 0;
	}
	else if(patch.IsObject())
	{
		uint64_t validate_begin = schema->size;
		static const char* del_key = JSONSCHEMA_PATCH_DELETION_LIST_KEYNAME;
		static const char* ins_key = JSONSCHEMA_PATCH_INSERTION_LIST_KEYNAME;
		if(patch.HasMember(del_key) && patch[del_key].IsArray())
		{
			uint64_t need_validate_begin = target_arr.Size();
			rapidjson::Value::Array del_arr = patch[del_key].GetArray();
			for(rapidjson::Value::ValueIterator iter = del_arr.Begin();
			    iter != del_arr.End();
			    iter ++)
			{
				if(!iter->IsUint64())
				    ERROR_RETURN_LOG(int, "Invalid patch format, deletion array should contains integers");
				uint64_t idx = iter->GetUint64();

				if(idx >= target_arr.Size())
				    ERROR_RETURN_LOG(int, "Invalid index to remove in an array");
				target_arr.Erase(target_arr.Begin() + idx);

				/* We requires the deletion operation list *strictly* descending,
				 * wihch means the patch maker should be responsible to sorting
				 * the deletion locations and make the largest comes first */
				if(need_validate_begin > idx)
				    need_validate_begin = idx;
				else
				    ERROR_RETURN_LOG(int, "The deletion array should be in desc order");
			}

			/* After deletion, all the items in the repeat zone should be
			 * valid still, however, we need to validate if the items in
			 * the non-repeated zone from the first item that has been affected.
			 * However, we can not validate the schema at this point, because
			 * it may be changed by the following insertion and modification
			 * section.
			 * Which means at this point, what we can do is to update the point
			 * we need to start the type validating
			 **/
			if(validate_begin > need_validate_begin)
			    validate_begin = need_validate_begin;
		}

		if(patch.HasMember(ins_key) && patch[ins_key].IsArray())
		{
			uint64_t need_validate_begin = target_arr.Size();
			rapidjson::Value::Array ins_arr = patch[ins_key].GetArray();
			for(rapidjson::Value::ValueIterator iter = ins_arr.Begin();
			    iter != ins_arr.End();
			    iter ++)
			{
				if(!iter->IsArray())
				    ERROR_RETURN_LOG(int, "Invalid patch format, an insertion record should be a a list");
				rapidjson::Value::Array ins_rec = iter->GetArray();

				if(ins_rec.Size() != 2 && !ins_rec[0].IsUint64())
				    ERROR_RETURN_LOG(int, "Invalid patch format, an insertion record should be [idx, value]");

				uint64_t idx = ins_rec[0].GetUint64();

				if(idx >= schema->size && !schema->repeat)
				    ERROR_RETURN_LOG(int, "Append an element to an fixed length array");

				if(idx >= schema->size - 1)
				{
					/* This means this the a schema should be in the repeated zone, so we validate it at this point */
					int rc = jsonschema_validate_obj(schema->element[schema->size - 1], ins_rec[1]);
					if(ERROR_CODE(int) == rc)
					    ERROR_RETURN_LOG(int, "Cannot validate the new elmeent");

					if(0 == rc)
					    ERROR_RETURN_LOG(int, "The insertion operation breaks the schema");
				}

				rapidjson::Value& val = ins_rec[1];

				target_arr.PushBack(val, allocator);

				val = target_arr[target_arr.Size() - 1];

				for(uint32_t i = target_arr.Size() - 1; i > idx; i --)
				{
					target_arr[i] = target_arr[i - 1];

					if(i + 1 == schema->size)
					{
						/* This means we are moving the affected thing from the non-repeated zone to repeated zone
						 * So we need validate it */
						int rc = jsonschema_validate_obj(schema->element[schema->size - 1], target_arr[i]);
						if(ERROR_CODE(int) == rc)
						    ERROR_RETURN_LOG(int, "Cannot validate the element");
						if(rc == 0)
						    ERROR_RETURN_LOG(int, "The insertion operation breaks the schema contract");
					}
				}

				target_arr[(unsigned)idx] = val;

				if(idx <= need_validate_begin)
				    need_validate_begin = idx;
				else
				    ERROR_RETURN_LOG(int, "The insertion operation array should be in desc order");
			}

			if(validate_begin > need_validate_begin)
			    validate_begin = need_validate_begin;

		}

		for(rapidjson::Value::MemberIterator iter = patch.MemberBegin();
		    iter != patch.MemberEnd();
		    iter ++)
		{
			if(strcmp(iter->name.GetString(), JSONSCHEMA_PATCH_DELETION_LIST_KEYNAME) == 0 ||
			   strcmp(iter->name.GetString(), JSONSCHEMA_PATCH_INSERTION_LIST_KEYNAME) == 0)
			    continue;

			char* end;

			long long idx = strtoll(iter->name.GetString(), &end, 0);
			if(*end != 0 || idx < 0 || idx >= target_arr.Size() || (idx > schema->size && !schema->repeat))
			    ERROR_RETURN_LOG(int, "Invalid offset in the array %s", iter->name.GetString());

			if(ERROR_CODE(int) == jsonschema_update_obj(schema->element[idx >= schema->size ? schema->size - 1 : idx],
			                                            target_arr[(unsigned)idx],
			                                            iter->value, allocator))
			    ERROR_RETURN_LOG(int, "Cannot patch the array member");

		}

		if((target_arr.Size() > schema->size && !schema->repeat) ||
		   (target_arr.Size() < schema->size - schema->repeat))
		    ERROR_RETURN_LOG(int, "The list patch breaks the schema");

		/* After that we need to revaliate some of the change caused by the deletion */
		uint64_t i;
		for(i = validate_begin; i < schema->size - 1 && i < target_arr.Size(); i ++)
		{
			int rc = jsonschema_validate_obj(schema->element[i], target_arr[(unsigned)i]);
			if(ERROR_CODE(int) == rc)
			    ERROR_RETURN_LOG(int, "Cannot validate the array after deletion");

			if(rc == 0) ERROR_RETURN_LOG(int, "List patch breaks the schema");
		}
		return 0;
	}
	else ERROR_RETURN_LOG(int, "Invalid patch type, either list or list diff exepcted");
}

static int _patch_obj(const _obj_t* schema, rapidjson::Value& target, rapidjson::Value& patch, rapidjson::Document::AllocatorType& allocator)
{
	static const char* cp_marker = JSONSCHEMA_PATCH_COMPLETED_MARKER;
	int override_directly = patch.HasMember(cp_marker) &&
	                        patch[cp_marker].IsTrue();

	if(override_directly)
	{
		patch.RemoveMember(cp_marker);

		/* At this point we need to validate the patch is a valid data insnace */
		int rc = _validate_obj(schema, 0, patch);
		if(ERROR_CODE(int) == rc)
		    ERROR_RETURN_LOG(int, "Cannot validate the patch data is well-formed");

		if(rc == 0)
		    ERROR_RETURN_LOG(int, "The patch breaks the data schema");

		target = patch;

		return 0;
	}

	/* Now we should follow the schema to make the update */
	uint32_t i;
	for(i = 0; i < schema->size; i ++)
	{
		if(!patch.HasMember(schema->element[i].key))
		    continue;
		if(!target.HasMember(schema->element[i].key))
		{
			rapidjson::Value null;
			rapidjson::Value key(schema->element[i].key, (unsigned)strlen(schema->element[i].key), allocator);
			target.AddMember(key, null, allocator);
		}
		if(ERROR_CODE(int) == jsonschema_update_obj(schema->element[i].val, target[schema->element[i].key], patch[schema->element[i].key], allocator))
		    ERROR_RETURN_LOG(int, "Cannot update member %s", schema->element[i].key);
		if(target[schema->element[i].key].IsNull())
		    patch.RemoveMember(schema->element[i].key);
	}

	return 0;
}

/****************** Exported functions ***************************/

int jsonschema_validate_obj(const jsonschema_t* schema, const rapidjson::Value& object)
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

int jsonschema_update_obj(const jsonschema_t* schema, rapidjson::Value& target, rapidjson::Value& patch, rapidjson::Document::AllocatorType& allocator)
{
	if(NULL == schema) ERROR_RETURN_LOG(int, "Invalid argument");

	if(target.IsNull())
	{
		LOG_DEBUG("This is the empty schema, so we only need to validate the remaining patch");
		int rc = jsonschema_validate_obj(schema, patch);
		if(rc == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot validate the JSON schema");

		if(0 == rc) ERROR_RETURN_LOG(int, "Invalid patch");

		target = patch;

		return 0;
	}
	else
	{
		if(schema->nullable && patch.IsNull())
		{
			if(target.IsNull()) return 0;
			rapidjson::Value null;
			target = null;
		}
		else switch(schema->type)
		{
			case _SCHEMA_TYPE_PRIMITIVE:
			    return _patch_primitive(schema->primitive, target, patch);
			case _SCHEMA_TYPE_LIST:
			    return _patch_list(schema->list, target, patch, allocator);
			case _SCHEMA_TYPE_OBJ:
			    return _patch_obj(schema->obj, target, patch, allocator);
			default:
			    ERROR_RETURN_LOG(int, "Code bug: Invalid schema type");
		}
	}

	return 0;
}

int jsonschema_update_obj(const jsonschema_t* schema, rapidjson::Document& target, rapidjson::Value& patch)
{
	return jsonschema_update_obj(schema, target, patch, target.GetAllocator());
}

extern "C" int jsonschema_free(jsonschema_t* schema)
{
	if(NULL == schema)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return _schema_free(schema);
}

extern "C" jsonschema_t* jsonschema_from_string(const char* schema_str)
{
	if(NULL == schema_str) ERROR_PTR_RETURN_LOG("Invalid arguments");

	rapidjson::Document document;
	rapidjson::MemoryStream ms(schema_str, strlen(schema_str));

	if(document.ParseStream(ms).HasParseError())
	    ERROR_PTR_RETURN_LOG("Invalid JSON object");

	jsonschema_t* ret = _jsonschema_new(document);
	return ret;
}

extern "C" jsonschema_t* jsonschema_from_file(const char* schema_file)
{
	jsonschema_t* ret = NULL;
	if(NULL == schema_file) ERROR_PTR_RETURN_LOG("Invalid arguments");

	FILE* fp = fopen(schema_file, "r");
	if(NULL == fp)
	    ERROR_PTR_RETURN_LOG("Cannot open schema file %s", schema_file);

	char buffer[65536];

	rapidjson::FileReadStream frs(fp, buffer, sizeof(buffer));
	rapidjson::Document document;

	if(document.ParseStream(frs).HasParseError())
	    ERROR_LOG_GOTO(ERR, "Invalid JSON object");

	ret = _jsonschema_new(document);
ERR:
	if(NULL != fp) fclose(fp);

	return ret;
}

extern "C" int jsonschema_validate_str(const jsonschema_t* schema, const char* input, size_t size)
{
	if(NULL == schema || NULL == input) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(size == 0) size = strlen(input);
	rapidjson::Document document;
	rapidjson::MemoryStream ms(input, strlen(input));

	if(document.ParseStream(ms).HasParseError())
	    ERROR_RETURN_LOG(int, "Invalid JSON input");

	return jsonschema_validate_obj(schema, document);
}

struct _BufferAllocator {
	static const bool kNeedFree = false;

	_BufferAllocator(void* mem, size_t size)
	{
		this->_mem = mem;
		this->_size = size;
	}

	_BufferAllocator()
	{
		this->_mem = NULL;
		this->_size = 0;
	}

	void* Malloc(size_t size)
	{
		(void)size;
		return this->_mem;
	}

	void* Realloc(void* originalPtr, size_t originalSize, size_t newSize)
	{
		(void)originalPtr;
		(void)originalSize;
		if(this->_size < newSize) ERROR_PTR_RETURN_LOG("Invalid arguments");

		if(newSize > this->_size) ERROR_PTR_RETURN_LOG("Out of memory");

		return this->_mem;
	}

	static void Free(void* ptr)
	{
		(void)ptr;
	}

	private:
	void*   _mem;
	size_t  _size;
};

extern "C" size_t jsonschema_update_str(const jsonschema_t* schema, const char* target, size_t target_len, const char* patch, size_t patch_len, char* outbuf, size_t bufsize)
{
	size_t rc = ERROR_CODE(size_t);
	if(NULL == schema || NULL == patch || NULL == outbuf)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	rapidjson::MemoryStream patch_ms(patch, patch_len > 0 ? patch_len : strlen(patch));
	rapidjson::Document patch_doc;
	if(patch_doc.ParseStream(patch_ms).HasParseError())
	    ERROR_RETURN_LOG(size_t, "Invalid JSON input");

	rapidjson::Document target_doc;

	if(NULL != target)
	{
		rapidjson::MemoryStream target_ms(target, target_len > 0 ? target_len : strlen(target));
		if(target_doc.ParseStream(target_ms).HasParseError())
		    ERROR_RETURN_LOG(size_t, "Invalid target JSONtext");
		if(ERROR_CODE(int) == jsonschema_update_obj(schema, target_doc, patch_doc))
		    ERROR_RETURN_LOG(size_t, "Cannot patch the target JSON object");

	}
	else if(ERROR_CODE(int) == jsonschema_update_obj(schema, target_doc, patch_doc))
	    ERROR_RETURN_LOG(size_t, "Cannot patch the null JSON object");

	_BufferAllocator allocator(outbuf, bufsize - 1);
	rapidjson::GenericMemoryBuffer<_BufferAllocator> output_ms(&allocator);
	rapidjson::Writer<rapidjson::GenericMemoryBuffer<_BufferAllocator> > writer(output_ms);

	target_doc.Accept(writer);

	outbuf[rc = output_ms.GetSize()] = 0;

	return rc;
}

#pragma GCC diagnostic pop
