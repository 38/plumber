/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The runtime values of PSS VM
 * @file pss/include/pss/value.h
 **/

#ifndef __PSS_VALUE_H__
#define __PSS_VALUE_H__
/**
 * @brief The callback functions used to manipulate a runtime value
 **/
typedef struct {
	/**
	 * @brief Make a new value from the given data pointer
	 * @param data The data pointer
	 * @note  This function is used when we want to convert a value to string.
	 *        And type_ops_string.mkval(str) will be called to make a new string
	 * @return The newly created data
	 **/
	void* (*mkval)(void* data);
	/**
	 * @brief Make a copy of the value
	 * @param value The value to copy
	 * @return The newly copied value
	 **/
	void* (*copy)(const void* value);  	
	/**
	 * @brief Dispose a unused value
	 * @param value The value to dispose
	 * @return status code
	 **/
	int   (*free)(void* value);
	/**
	 * @brief Convert the value to it's string representation
	 * @param value The value to convert
	 * @param buf The buffer for the string 
	 * @param bufsize The size of the buffer
	 * @return The result string
	 **/
	const char* (*tostr)(const void* value, char* buf, size_t bufsize);
} pss_value_ops_t;

/**
 * @brief The enum indicates what type of value it is
 **/
typedef enum {
	PSS_VALUE_TYPE_DICT,    /*!< A dictionary */
	PSS_VALUE_TYPE_STRING,  /*!< A string */
	PSS_VALUE_TYPE_CLOSURE, /*!< A closure */
	PSS_VALUE_TYPE_TEST     /*!< The type reserved for test cases */
} pss_value_type_t;

/**
 * @brief The data structure for a runtime value
 **/
typedef struct _pss_value_t pss_value_t;

/**
 * @brief Make a new runtime value 
 * @param type The type of the value
 * @param data The data of the value
 * @note Once we create a value and do not assigned this is marked as inactive and 0 refcnt, and
 *       The 0 refcnt will prevent the value from being disposed. However, onces incref is called
 *       the value will become active, and since then the reference counter will collect the value
 *       once it's not used anymore
 * @return The newly created value
 **/
pss_value_t* pss_value_new(pss_value_type_t type, void* data);

/**
 * @brief Increase the reference counter of the value
 * @param value The value 
 * @return status code 
 **/
int pss_value_incref(pss_value_t* value);

/**
 * @brief Decrease the reference counter of the value
 * @param vvalue The value to oeprate
 * @return status code
 **/
int pss_value_decref(pss_value_t* value);

/**
 * @brief Make a copy of the value
 * @param value The value to operate
 * @reteurn The newly created value
 **/
pss_value_t* pss_value_copy(const pss_value_t* value);

/**
 * @brief Convert the value to string type and make a new value for it
 * @param value The value to convert
 * @note  If the 
 * @return The newly create value
 **/
pss_value_t* pss_value_to_str(const pss_value_t* value);

/**
 * @brief Set the type specified operations for the given type
 * @param type The type code
 * @param ops  The operations
 * @return status code
 **/
int pss_value_set_type_ops(pss_value_type_t type, pss_value_ops_t ops);

#endif 
