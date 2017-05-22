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
	LANG_PROP_TYPE_STRING,    /*!< a string property */
	LANG_PROP_TYPE_INTEGER,    /*!< a interger property */
	LANG_PROP_TYPE_COUNT
} lang_prop_type_t;


/**
 * @brief a callback vector object
 **/
typedef struct _lang_prop_callback_vector_t lang_prop_callback_vector_t;

/**
 * @brief the callback function used to get a value of a symbol
 * @param param the addition callback param
 * @param cvb the callback vector
 * @param nsect how many sections in the symbol
 * @param symbol the symbol to get
 * @param buffer the result buffer
 * @note Once you have an request for a string typed property, you should allocate
 *       memory using malloc, and the interpreter will automatically call dispose when
 *       it's no longer using. Do not pass any static memory or heap memory back to this function
 * @return the number of property that returned or error code
 **/
typedef int (*lang_prop_get_func_t)(const lang_prop_callback_vector_t* cbv, const void* param, uint32_t nsect, const uint32_t* symbol, lang_prop_type_t type, void* buffer);

/**
 * @brief the callback function used to set value of a symbol
 * @param cbv the callback vector
 * @param param the additional callback param
 * @param nsect how many sections
 * @param symbol the symbol to set
 * @param data the data to set
 * @note for the string reference, the function should keep its own copy, because
 *       it's not well defined when the value in the vm gets deallocated. <br/>
 * @return the number of propety that modified or error code
 **/
typedef int (*lang_prop_set_func_t)(const lang_prop_callback_vector_t* cbv, const void* param, uint32_t nsect, const uint32_t* symbol, lang_prop_type_t type, const void* data);

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
 * @brief represents a property value
 **/
typedef union {
	int32_t integer;   /*!< a integer */
	const char* string; /*!< a string */
} lang_prop_value_t;

/**
 * @brief initialize the prop utils
 * @return status code
 **/
int lang_prop_init();

/**
 * @brief finalize the prop utils
 * @return the status code
 **/
int lang_prop_finalize();

/**
 * @brief create a new callback vector
 * @return the newly created vector, NULL if there's an error
 **/
lang_prop_callback_vector_t* lang_prop_callback_vector_new(const lang_bytecode_table_t* bc_table);

/**
 * @brief dispose the used callback vector
 * @param vector the target vector4
 * @return the status code
 **/
int lang_prop_callback_vector_free(lang_prop_callback_vector_t* vector);

/**
 * @brief add a new call back function to the prop
 * @param callback the callback function
 * @return status code
 **/
int lang_prop_register_callback(const lang_prop_callback_t* callback);

/**
 * @brief get a symbol in the property table
 * @param callbacks the callback vector
 * @param symid the symbol id to handle
 * @param type the result type
 * @param buffer the result buffer
 * @return status code
 **/
int lang_prop_get(const lang_prop_callback_vector_t* callbacks, uint32_t symid, lang_prop_type_t* type, lang_prop_value_t* buffer);

/**
 * @brief set a symbol in the property table
 * @param symid the symbol id to handle
 * @param callbacks the callback vector
 * @param type the type of this data
 * @param data the data to set
 * @return status code
 **/
int lang_prop_set(const lang_prop_callback_vector_t* callbacks, uint32_t symid, lang_prop_type_t type, const void* data);

/**
 * @brief get the symbol's string representation by the symbol string id
 * @param callbacks the target callbacks
 * @param strid the string id
 * @return the result string, NULL if there's an error
 **/
const char* lang_prop_get_symbol_string(const lang_prop_callback_vector_t* callbacks, uint32_t strid);

#endif /* __PLUMBER_LANG_PROP_H__ */
