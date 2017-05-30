/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the assertion utils
 * @file proto/include/proto/assert.h
 **/
#ifndef __PROTO_ASSERT_H__
#define __PROTO_ASSERT_H__

/**
 * @brief assert the code must have the condition, if not, execute the action code
 * @param cond the condition
 * @param action the action on the condition breaks
 **/
#ifndef __PROTO_NODEBUG__
#	define ASSERT_ACTION(cond, action) do {\
	    if(!(cond))\
	    {\
		    action;\
	    }\
    } while(0)
#else
#	define ASSERT_ACTION(cond, action) do { } while (0)
#endif

/**
 * @brief assert the code must meet the condition
 * @param cond the condition
 * @param type the function return type
 **/
#define ASSERT_RETURN(cond, type) ASSERT_ACTION(cond, PROTO_ERR_RAISE_RETURN(type, BUG))

/**
 * @brief assert the code must meet the condtion, if failed, return NULL pointer
 * @param cond the condition
 **/
#define ASSERT_RETURN_PTR(cond) ASSERT_ACTION(cond, PROTO_ERR_RAISE_RETURN_PTR(BUG))

/**
 * @brief assert the code must meet the condition if failed, goto the given label
 * @param cond the condition
 * @param label the label
 **/
#define ASSERT_GOTO(cond, label) ASSERT_ACTION(cond, PROTO_ERR_RAISE_GOTO(label, BUG))

#endif /* __PROTO_ASSERT_H__ */
