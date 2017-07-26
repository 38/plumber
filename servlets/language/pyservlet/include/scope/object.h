/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The RLS scope object wrapper
 * @file scope/object.h
 **/
#ifndef __SCOPE_OBJECT_H__
#define __SCOPE_OBJECT_H__

/**
 * @brief The type code for the scope object
 **/
typedef enum {
	SCOPE_OBJECT_TYPE_STRING,   /*!< This scope object is a string */
	SCOPE_OBJECT_TYPE_FILE,     /*!< This scope object is a file */
	SCOPE_OBJECT_TYPE_COUNT     /*!< The number of the scope object type */
} scope_object_type_t;

typedef struct {
	const char* name;   /*!< The name of this type */
	/**
	 * @brief Create a new scope object in the specified type
	 * @param args The creation parameter we need to used
	 * @return The newly created RLS scope object
	 **/
	void* (*create)(PyObject* args);

	/**
	 * @brief Dispose the scope object
	 * @param The object we need to dispose
	 * @return status code
	 **/
	int   (*dispose)(void* obj);

	/**
	 * @brief Commit the scope object to the RLS
	 * @param object The object to commit
	 * @return The scope token
	 **/
	scope_token_t (*commit)(void* object);

	/* TODO: Copy the RLS */

} scope_object_ops_t;

/**
 * @brief Check if the scope object is expected type
 * @param val The scope object value
 * @param type The expected type code
 * @return The check result
 **/
#define SCOPE_OBJECT_IS_TYPE(val, type) (((scope_object_info_t*)(&val))->type_code == type)

/**
 * @brief Initialize the scope object API
 * @param module The python module we want to initialize the API
 * @return status code
 **/
int scope_object_init(PyObject* module);

/**
 * @brief Register a type operations for the given scope object type
 * @param type The type
 * @param ops The operation definitoin
 * @return status code
 **/
int scope_object_register_type_ops(scope_object_type_t type, scope_object_ops_t ops);

/**
 * @brief Retrieve the RLS object from the Python object
 * @param type The expected RLS type
 * @param object The python object
 * @return The pointer to the RLS object
 **/
const void* scope_object_retrieve(scope_object_type_t type, PyObject* object);

#endif
