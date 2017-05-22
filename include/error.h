/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the error code defination
 * @file error.h
 **/
#ifndef __PLUMBER_ERROR_H__
#define __PLUMBER_ERROR_H__

/**
 * @brief the error code of type type
 * @param type the type name
 * @return the error code
 **/
#define ERROR_CODE(type) ((type)(-1))

/**
 * @brief the error code that takes the ownership of the input params (A.K.A error code with owership transfer)
 * @note this is another form of error code, but the difference is we use
 *       this error code indicates, the operation is failed, however, the
 *       pointer has been passed in is already disposed by the operation.
 *       So you have lost the ownership of the passed in pointers anyway. <br/>
 *       This is only used in the tricky situation like the DRA callbacks, because
 *       we may have the case that the DRA callback has been disposed, then we got
 *       a write failure. In this case, this callback is used to make sure that we won't
 *       disdpose the disposed callbacks. <br/>
 *       When we want a funcion use this, we need to mention the error code with owership
 *       transfer or alternatively error code OT is a possible result
 * @param type the typename
 * @return the error code
 **/
#define ERROR_CODE_OT(type) ((ERROR_CODE(type)) ^ ((type)1))

/**
 * @brief perform the action and return an error code
 * @param type the type of the function
 * @param action the action to take
 **/
#define ERROR_RETURN_ACTION(type, action) do{\
	action;\
	return ERROR_CODE(type);\
} while(0)

/**
 * @brief log a error message and return error code
 * @param type the type of the function
 * @param msg the message string
 * @param arg the arguments
 **/
#define ERROR_RETURN_LOG(type, msg, arg...) ERROR_RETURN_ACTION(type, LOG_ERROR(msg, ##arg))

/**
 * @brief log a errno message and return error code
 * @param type the type of the function
 * @param msg the message string
 * @param arg the arguments
 **/
#define ERROR_RETURN_LOG_ERRNO(type, msg, arg...) ERROR_RETURN_ACTION(type, LOG_ERROR_ERRNO(msg, ##arg))

/**
 * @brief return error code
 * @param type the type of the function
 **/
#define ERROR_RETURN(type) ERROR_RETURN_ACTION(type,/*NOP*/)

/**
 * @brief a pointer type error code
 **/
#define ERROR_PTR NULL

/**
 * @brief perform an action then return a pointer error code
 * @param action the action to perform
 **/
#define ERROR_PTR_RETURN_ACTION(action) do{\
	action;\
	return NULL;\
} while(0)

/**
 * @brief log an error message and return error pointer
 * @param msg the message
 * @param arg the argument
 **/
#define ERROR_PTR_RETURN_LOG(msg, arg...) ERROR_PTR_RETURN_ACTION(LOG_ERROR(msg, ##arg))

/**
 * @brief log an errno and return error pointer
 * @param msg the message
 * @param arg the arguments
 **/
#define ERROR_PTR_RETURN_LOG_ERRNO(msg, arg...) ERROR_PTR_RETURN_ACTION(LOG_ERROR_ERRNO(msg, ##arg))

/**
 * @brief log an error message and goto a label
 * @param label the target label
 * @param msg the message
 * @param arg the arguments
 **/
#define ERROR_LOG_GOTO(label, msg, arg...) do {\
	LOG_ERROR(msg, ##arg);\
	goto label;\
} while(0)

/**
 * @brief log an errno message an goto a label
 * @param label the target label
 * @param msg the message
 * @param arg the arguments
 **/
#define ERROR_LOG_ERRNO_GOTO(label, msg, arg...) do {\
	LOG_ERROR_ERRNO(msg, ##arg);\
	goto label;\
} while(0)

#endif /* __PLUMBER_ERROR_H__ */
