/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief   The error handling utils for the protocol type library
 * @details Because the protocol type library do not actually use the libplumber's logging util.
 *          We cannot use the error macro at this point. So we designed a mininal error handling
 *          utils at this point <br/>
 *          The protocol type library will still use the error code convention like other part of plumber,
 *          however, instead of dumping a log about the error, the library will create an error stack about
 *          the error at this point. <br/>
 *          The error utils is thread safe, which means the error stack is a thread local
 * @file proto/include/proto/err.h
 **/
#ifndef __PROTO_ERR_H__
#define __PROTO_ERR_H__

#include <error.h>

/**
 * @brief represents the class of the error code
 * @details for the meaning of the error code, see err.c
 **/
typedef enum {
	PROTO_ERR_CODE_ALLOC,
	PROTO_ERR_CODE_OPEN,
	PROTO_ERR_CODE_READ,
	PROTO_ERR_CODE_WRITE,
	PROTO_ERR_CODE_FILEOP,
	PROTO_ERR_CODE_FORMAT,
	PROTO_ERR_CODE_ARGUMENT,
	PROTO_ERR_CODE_FAIL,
	PROTO_ERR_CODE_DISALLOWED,
	PROTO_ERR_CODE_NOT_FOUND,
	PROTO_ERR_CODE_CIRULAR_DEP,
	PROTO_ERR_CODE_NAME_EXPR,
	PROTO_ERR_CODE_BUG,
	PROTO_ERR_CODE_UNDEFINED,
	PROTO_ERR_CODE_DIM,
	PROTO_ERR_CODE_OUT_OF_BOUND,
	PROTO_ERR_CODE_VERSION,
	PROTO_ERR_CODE_COUNT
} proto_err_code_t;

/**
 * @brief an actual protocol library error
 **/
typedef struct _proto_err_t {
	proto_err_code_t               code;  /*!< the error code */
	int                            errnum;/*!< the errorno when the error happens */
	uint32_t                       line;  /*!< the line number that raises this error */
	const char*                    file;  /*!< the source code filename that raises this error, the filename shouldn't be a dynamically allocated value */
	struct _proto_err_t const *    child; /*!< the child error */
} proto_err_t;

/**
 * @brief get the protocol error stack
 * @return the protocol error stack, NULL if no error set
 **/
const proto_err_t* proto_err_stack();

/**
 * @brief clear the protocol error stack
 * @note in order to avoid the still-reachable leak when exit, make sure all the error message is cleared
 *       at the time exit
 * @return void
 **/
void proto_err_clear();

/**
 * @brief raise a new error
 * @note the file param should be a string constant, otherwise memory leak or corruption is possible <br/>
 *       The firstly raised error will contains a snapshot of errno, so that we can analyze the cause of this error
 * @param code the error code
 * @param line the current line number
 * @param file the file name
 * @return nothing
 **/
void proto_err_raise(proto_err_code_t code, uint32_t line, const char* file);

/**
 * @brief get the human readable error description from the error
 * @param error the error object
 * @param buffer the output buffer
 * @param bufsize the size of the output buffer
 * @return NULL on error case or the human-readable error message
 **/
const char* proto_err_str(const proto_err_t* error, char* buffer, size_t bufsize);

/**
 * @brief raise an error and return the function with the error code
 * @param ret_type the return type of current function
 * @param err_type the error type code
 **/
#define PROTO_ERR_RAISE_RETURN(ret_type, err_type) do {\
	proto_err_raise(PROTO_ERR_CODE_##err_type, __LINE__, __FILE__);\
	return ERROR_CODE(ret_type);\
}while(0)

/**
 * @brief raise an error and return an NULL pointer
 * @note this is similar to PROTO_ERR_RAISE_RETURN for a function returns a pointer
 * @param err_type the error type code
 **/
#define PROTO_ERR_RAISE_RETURN_PTR(err_type) do {\
	proto_err_raise(PROTO_ERR_CODE_##err_type, __LINE__, __FILE__);\
	return NULL;\
}while(0)

/**
 * @brief raise an error and goto the error handling label
 * @param label the label to goto
 * @param err_type the error type code
 **/
#define PROTO_ERR_RAISE_GOTO(label, err_type) do {\
	proto_err_raise(PROTO_ERR_CODE_##err_type, __LINE__, __FILE__);\
	goto label;\
}while(0)

#endif /* __PROTO_ERR_H__ */

