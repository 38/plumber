/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The top level compiler interfaces
 * @file  pss/comp/compiler.h
 **/
#include <utils/static_assertion.h>
#ifndef __PSS_COMP_COMP_H__
#define __PSS_COMP_COMP_H__

#define PSS_COMP_RAISE_ACTION(action, comp, msg, args...) do {\
	pss_comp_raise(comp, msg, ##args);\
	LOG_ERROR(msg, ##args);\
	action;\
} while(0)

#define PSS_COMP_RAISE_RETURN(type, comp, msg, args...) PSS_COMP_RAISE_ACTION(return ERROR_CODE(type), comp, msg, ##args)

#define PSS_COMP_RAISE_RETURN_PTR(comp, msg, args...) PSS_COMP_RAISE_ACTION(return NULL, comp, msg, ##args)

#define PSS_COMP_RAISE_GOTO(label, comp, msg, args...) PSS_COMP_RAISE_ACTION(goto label, comp, msg, ##args)

/**
 * @brief Represent an options for a parser
 **/
typedef struct {
	pss_bytecode_module_t*  module;   /*!< The module we want output the code to */
	pss_comp_lex_t*         lexer;    /*!< The lexer we are using for the code */
	uint32_t                debug:1;  /*!< If we need include debug info in the bytecode */
} pss_comp_option_t;

/**
 * @brief The compiler error data structure
 **/
typedef struct _pss_comp_error_t {
	const char* filename;             /*!< The file name */
	uint32_t    line;                 /*!< The line number */
	struct _pss_comp_error_t* next;   /*!< The next compile error */
	uintptr_t   __padding__[0];
	char        message[0];           /*!< The actual error message */
} pss_comp_error_t;
STATIC_ASSERTION_LAST(pss_comp_error_t, message);
STATIC_ASSERTION_SIZE(pss_comp_error_t, message, 0);

/**
 * @brief The PSS compiler instance
 **/
typedef struct _pss_comp_t pss_comp_t;

/**
 * @brief Create a new compiler from the given option
 * @param option The compiler option
 * @param error The buffer used to return the error list
 * @return The newly created compiler
 **/
int pss_comp_compile(pss_comp_option_t* option, pss_comp_error_t** error);

/**
 * @brief Dispose the error list
 * @param error The error list to dispose
 * @return status code
 **/
int pss_comp_free_error(pss_comp_error_t* error);

/**
 * @brief Peek the next N-th token in from the compiler's lexer
 * @param comp The compiler instance
 * @Param N The n-th token
 * @return The token peeked
 **/
const pss_comp_lex_token_t* pss_comp_peek(pss_comp_t* comp, uint32_t n);

/**
 * @brief Consume the first N tokens from the lexer stream
 * @param n The number of stream to consume
 * @param comp The compiler instance
 * @return status code
 **/
int pss_comp_comsume(pss_comp_t* comp, uint32_t n);

/**
 * @brief Get the bytecode module the compiler is writting to
 * @param comp The compiler
 * @return The module
 **/
pss_bytecode_segment_t* pss_comp_get_code_segment(pss_comp_t* comp);

/**
 * @brief Get the current compiler abstracted environment
 * @param comp The compiler instance
 * @return The result environment
 **/
pss_comp_env_t* pss_comp_get_env(pss_comp_t* comp);

/**
 * @brief Open a new closure with N arguments
 * @param comp The compiler instance
 * @param nargs The arguments
 * @param argnames The name list for the arguments
 * @return status code
 **/
int pss_comp_open_closure(pss_comp_t* comp, uint32_t nargs, char const** argnames);

/**
 * @brief Close the current closure and return the segment ID in the bytecode module
 * @param comp The compiler instance
 * @return The bytecode segment id
 **/
pss_bytecode_segid_t pss_comp_close_closure(pss_comp_t* comp);


/**
 * @brief Open a new control block, a control block is the block which contains a begin and end label, so that
 *        we can performe jump instruction to the code block
 * @param comp The compiler instance
 * @return status code
 **/
int pss_comp_open_control_block(pss_comp_t* comp);

/**
 * @brief Close the last control block
 * @param comp The compiler instance
 * @return status code
 **/
int pss_comp_close_control_block(pss_comp_t* comp);

/**
 * @biref The get begin address of the last control block, this is useful when we have a continue instruction
 * @param comp The compiler instance
 * @return status code
 **/
pss_bytecode_addr_t pss_comp_last_control_block_begin(pss_comp_t* comp);

/**
 * @brief Get the end label of the last control block
 * @param comp The compiler instance
 * @return status code
 **/
pss_bytecode_label_t pss_comp_last_control_block_end(pss_comp_t* comp);


/**
 * @brief raise an compiler error with the given message, the line number
 *        will be automatically set to current value
 * @param comp The compiler instance
 * @param msg The message
 * @return status code
 **/
int pss_comp_raise(pss_comp_t* comp, const char* msg, ...)
	__attribute__((format(printf,2,3)));

#endif
