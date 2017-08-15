/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <json.h>
#include <jsonschema.h>

/**
 * @brief The type of the schema element
 **/
typedef enum {
	_ELEMTYPE_OPEN_OBJ,   /*!< The element that used to open an object */
	_ELEMTYPE_CLOSE_OBJ,  /*!< The element that close an object */
	_ELEMTYPE_OPEN_LIST,  /*!< Open an list */
	_ELEMTYPE_CLOSE_LIST, /*!< Close a list */
	_LEMETYPE_PRIMITIVE   /*!< The primitive type */
} _elemtype_t;

/**
 * @brief The type descriptor in a type
 **/
typedef enum {
	_PRIMITIVE_TYPE_NUMBER = 1,   /*!< The number type */
	_PRIMITIVE_TYPE_STRING = 2,   /*!< The string type */
	_PRIMITIVE_TYPE_NULL   = 4,   /*!< This is a primitive */
	_PRIMITIVE_TYPE_BOOLEAN = 8   /*!< This is a boolean type */
} _primitive_type_t;

/**
 * @brief The actual schema element 
 **/
typedef struct {
	_elemtype_t type;  /*!< The element type */
	union {
		char*    key;   /*!< The key of the element, only valid when we are currently in an object */
		uint32_t idx;   /*!< The index in the list, only vlaid when we are currently in an list */
	}            addr;  /*!< The address in current environment */
	_primitive_type_t primitvie;   /*!< This field only valid when the element is an primitve */
} _element_t;

/**
 * @brief The actual data struture for a JSON schema 
 **/
struct _jsonschema_t {
	uint32_t capacity;   /*!< The capacity of the schema element */
	uint32_t count;      /*!< The number of the schema element current in the JSON schema object */
	_element_t* elem;    /*!< The element list */
};

int jsonchema_init()
{
	return 0;
}

int jsonschema_fianlize()
{
	return 0;
}
