/**
 * Copyright (C) 2017, Hao Hou
 * Copyright (C) 2017, Feng Liu
 **/

/**
 * @brief The runtime values of PSS VM
 * @file pss/include/pss/value.h
 **/

#include <utils/static_assertion.h>

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
} pss_value_ref_ops_t;

/**
 * @brief The enum indicates what type of value reference it is
 **/
typedef enum {
	PSS_VALUE_REF_TYPE_ERROR = -1,/*!< Invalid type code */
	PSS_VALUE_REF_TYPE_DICT,      /*!< A dictionary */
	PSS_VALUE_REF_TYPE_STRING,    /*!< A string */
	PSS_VALUE_REF_TYPE_CLOSURE,   /*!< A closure */
	PSS_VALUE_REF_TYPE_EXOTIC,    /*!< A external builtin object */
	PSS_VALUE_REF_TYPE_TEST,      /*!< The type reserved for test cases */
	PSS_VALUE_REF_TYPE_COUNT      /*!< The number of value reference count */
} pss_value_ref_type_t;

/**
 * @brief The data structure for a reference runtime value
 **/
typedef struct _pss_value_ref_t pss_value_ref_t;

/**
 * @brief The enum indicates what value it is
 **/
typedef enum {
	PSS_VALUE_KIND_UNDEF,   /*!< This is a undefined value */
	PSS_VALUE_KIND_NUM,     /*!< This is a numeric type */
	PSS_VALUE_KIND_BUILTIN, /*!< A builtin function */
	PSS_VALUE_KIND_REF,     /*!< This is an object reference */
	PSS_VALUE_KIND_ERROR = -1 /*!< Indicates it's an error */
} pss_value_kind_t;
/**
 * @brief By default the value should be undefined
 **/
STATIC_ASSERTION_EQ(PSS_VALUE_KIND_UNDEF, 0);

/**
 * @brief The previous definition of the virtual machine runtime value
 **/
typedef struct _pss_value_t pss_value_t;

typedef struct _pss_vm_t pss_vm_t;

/**
 * @brief The type for the builtin function
 * @param argc The number of arguments
 * @param argv The actual argument list
 * @return The return value
 **/
typedef pss_value_t (*pss_value_builtin_t)(pss_vm_t* vm, uint32_t argc, pss_value_t* argv);

/**
 * @brief A runtime value
 * @note  This is the mutable version of the runtime value. It means we are able to
 *        change the reference counter
 **/
struct _pss_value_t {
	pss_value_kind_t               kind;       /*!< What kind of value it is */
	union {
		pss_value_ref_t*           ref;        /*!< A reference value */
		pss_bytecode_numeric_t     num;        /*!< A numeric value */
		pss_value_builtin_t        builtin;    /*!< A builtin function */
	};
};

/**
 * @brief Create a new error value
 * @return The value indicates error
 **/
static inline pss_value_t pss_value_err()
{
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR
	};

	return ret;
}

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
pss_value_t pss_value_ref_new(pss_value_ref_type_t type, void* data);

/**
 * @brief Get the type code of the value reference
 * @note If the value is not a refernce, return PSS_VALUE_REF_TYPE_ERROR
 * @param value The value to check
 * @return The value code
 **/
pss_value_ref_type_t pss_value_ref_type(pss_value_t value);

/**
 * @brief Increase the reference counter of the value
 * @param value The value
 * @return status code
 **/
int pss_value_incref(pss_value_t value);

/**
 * @brief Decrease the reference counter of the value
 * @param vvalue The value to oeprate
 * @return status code
 **/
int pss_value_decref(pss_value_t value);

/**
 * @brief Convert the value to string type and make a new value for it
 * @param value The value to convert
 * @note  If the
 * @return The newly create value
 **/
pss_value_t pss_value_to_str(pss_value_t value);

/**
 * @brief Stringify the value to the given string buffer
 * @note This function is mostly like the to_str function, but it writes
 *       the result to the given string buffer <br/>
 *       This function will put a trailing 0 if there's at least one bytes space in the buffer
 * @param value The value to stringify
 * @param buf The buffer we should use
 * @param sz  The size of the buffer
 * @return The number of bytes has written (excluded the trailing 0)
 **/
size_t pss_value_strify_to_buf(pss_value_t value, char* buf, size_t sz);

/**
 * @brief Set the type specified operations for the given type
 * @param type The type code
 * @param ops  The operations
 * @return status code
 **/
int pss_value_ref_set_type_ops(pss_value_ref_type_t type, pss_value_ref_ops_t ops);

/**
 * @brief Get the data pointer for this value
 * @param value The value
 * @return The data pointer
 **/
void* pss_value_get_data(pss_value_t value);

/**
 * @brief Kill current execution 
 * @param target vm
 * @return status code
 **/
int pss_vm_kill(pss_vm_t* vm);

#endif
