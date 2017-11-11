/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The property table, this is used to define the special variable that is related
 *        to other parts of the project
 * @file lang/prop.h
 **/
#ifndef __PLUMBER_LANG_PROP_H__
#define __PLUMBER_LANG_PROP_H__

/**
 * @brief the type of a property
 **/
typedef enum {
	LANG_PROP_TYPE_ERROR = -1,/*!< An error value */
	LANG_PROP_TYPE_NONE,      /*!< Indicates the property system can not handle this symbol */
	LANG_PROP_TYPE_STRING,    /*!< a string property */
	LANG_PROP_TYPE_INTEGER,   /*!< a interger property */
	LANG_PROP_TYPE_COUNT
} lang_prop_type_t;

/**
 * @brief represents a property value
 **/
typedef struct {
	lang_prop_type_t type; /*!< The type of this property */
	union {
		int64_t     num;   /*!< a integer */
		char*       str;   /*!< a string */
	};
} lang_prop_value_t;


/**
 * @brief the callback function used to get a value of a symbol
 * @param param the addition callback param
 * @param symbol The symbol to get (The prefix has already been stripped)
 * @note Once you have an request for a string typed property, you should allocate
 *       memory using malloc, and the interpreter will automatically call dispose when
 *       it's no longer using. Do not pass any static memory or heap memory back to this function
 * @return the number of property that returned or error code
 **/
typedef lang_prop_value_t (*lang_prop_get_func_t)(const char* symbol, const void* param);

/**
 * @brief the callback function used to set value of a symbol
 * @param param the additional callback param
 * @param symbol the symbol to set
 * @param value The value to set
 * @note for the string reference, the function should keep its own copy, because
 *       it's not well defined when the value in the vm gets deallocated. <br/>
 * @return the number of propety that modified or error code
 **/
typedef int (*lang_prop_set_func_t)(const char* symbol, lang_prop_value_t value, const void* param);

/**
 * @brief the callback function used to handle the property
 **/
typedef struct {
	const void*        param;    /*!< the getter or setter param */
	lang_prop_get_func_t get;    /*!< the getter function */
	lang_prop_set_func_t set;    /*!< the setter function */
	const char* symbol_prefix;   /*!< the prefix of the symbol this handler matches */
} lang_prop_callback_t;

/**
 * @brief initialize the prop utils
 * @return status code
 **/
int lang_prop_init(void);

/**
 * @brief finalize the prop utils
 * @return the status code
 **/
int lang_prop_finalize(void);

/**
 * @brief add a new call back function to the prop
 * @param callback the callback function
 * @return status code
 **/
int lang_prop_register_callback(const lang_prop_callback_t* callback);

/**
 * @brief get a symbol in the property table
 * @param symbol The symbol
 * @return status code
 **/
lang_prop_value_t lang_prop_get(const char* symbol);

/**
 * @brief set a symbol in the property table
 * @param symbol The symbol to read
 * @param value The value
 * @return The number of value has been written, or error code
 **/
int lang_prop_set(const char* symbol, lang_prop_value_t value);

#endif /* __PLUMBER_LANG_PROP_H__ */
