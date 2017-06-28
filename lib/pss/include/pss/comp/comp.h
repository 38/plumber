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

#define PSS_COMP_RAISE_SYN_ACTION(action, comp, msg, args...) do {\
	pss_comp_raise(comp, "Syntax error: "msg, ##args);\
	LOG_ERROR(msg"@(%u:%u,%s)", ##args, pss_comp_peek(comp, 0)->line, pss_comp_peek(comp, 0)->offset, pss_comp_peek(comp, 0)->file);\
	action;\
} while(0)

#define PSS_COMP_RAISE_SYN(type, comp, msg, args...) PSS_COMP_RAISE_SYN_ACTION(return ERROR_CODE(type), comp, msg, ##args)

#define PSS_COMP_RAISE_SYNT_PTR(comp, msg, args...) PSS_COMP_RAISE_SYN_ACTION(return NULL, comp, msg, ##args)

#define PSS_COMP_RAISE_SYN_GOTO(label, comp, msg, args...) PSS_COMP_RAISE_SYN_ACTION(goto label, comp, msg, ##args)

#define PSS_COMP_RAISE_INT(comp, reason) do{ return pss_comp_raise_internal(comp, PSS_COMP_INTERNAL_##reason); } while(0)

#define PSS_COMP_RAISE_INT_GOTO(label, comp, reason) do { pss_comp_raise_internal(comp, PSS_COMP_INTERNAL_##reason); goto label; } while(0)

typedef enum {
	PSS_COMP_INTERNAL_CODE,    /*!< We cannot append code to the instruction */
	PSS_COMP_INTERNAL_SEG,     /*!< We cannot acquire the bytecode segment to output */
	PSS_COMP_INTERNAL_MALLOC,  /*!< We cannot allocate memory normally */
	PSS_COMP_INTERNAL_ARGS,    /*!< We have some invalid arguments */
	PSS_COMP_INTERNAL_BUG      /*!< This is some compiler bug situation */
} pss_comp_internal_t;

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
	uint32_t    column;               /*!< The column number */
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
int pss_comp_consume(pss_comp_t* comp, uint32_t n);

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
 * @param id The function name
 * @return status code
 **/
int pss_comp_open_closure(pss_comp_t* comp, const char* id, uint32_t nargs, char const** argnames);

/**
 * @brief Close the current closure and return the segment ID in the bytecode module
 * @param comp The compiler instance
 * @return The bytecode segment id
 **/
pss_bytecode_segid_t pss_comp_close_closure(pss_comp_t* comp);

/**
 * @brief Open and enter a new scope
 * @param comp The compilter
 * @return status code
 **/
int pss_comp_open_scope(pss_comp_t* comp);

/**
 * @brief Close and leave current scope, this makes all the local variable registers released
 * @param comp The compiler
 * @return status code
 **/
int pss_comp_close_scope(pss_comp_t* comp);

/**
 * @brief Get the local variable
 * @details This function also used to check if we should access the global storage as well, 
 * @param comp The compiler instance
 * @param var  The variable name
 * @param resbuf the buffer used to return the register id assigned to this local variable
 * @return The number of variables has been found, 1 if we found a local variable, 0 if we should access the global
 *         error code when the function can not finish successfully
 **/
int pss_comp_get_local_var(pss_comp_t* comp, const char* var, pss_bytecode_regid_t* resbuf);

/**
 * @brief Declare a local variable in current scope
 * @param comp The compilter instance
 * @param var The variable name
 * @return The register assigned to this variable
 **/
pss_bytecode_regid_t pss_comp_decl_local_var(pss_comp_t* comp, const char* var);

/**
 * @brief Make a temporory register 
 * @parap comp The compiler instance
 * @return The register id
 **/
pss_bytecode_regid_t pss_comp_mktmp(pss_comp_t* comp);

/**
 * @biref Rerlease a temp register
 * @param regid The register to release
 * @return status code
 **/
int pss_comp_rmtmp(pss_comp_t* comp, pss_bytecode_regid_t regid);

/**
 * @brief Open a new control block, a control block is the block which contains a begin and end label, so that
 *        we can performe jump instruction to the code block
 * @param comp The compiler instance
 * @return status code
 **/
int pss_comp_open_control_block(pss_comp_t* comp, int loop);

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
pss_bytecode_addr_t pss_comp_last_control_block_begin(pss_comp_t* comp, uint32_t n);

/**
 * @brief Get the end label of the last control block
 * @param comp The compiler instance
 * @return status code
 **/
pss_bytecode_label_t pss_comp_last_control_block_end(pss_comp_t* comp, uint32_t n);

/**
 * @brief Get the address where the nearest loop begins (this means we are currently in the loop block)
 * @param comp The compiler
 * @return The result
 **/
pss_bytecode_addr_t pss_comp_last_loop_begin(pss_comp_t* comp);

/**
 * @brief Get the label where the nearest loop ends 
 * @param comp The compiler
 * @return The label for the end of the loop
 **/
pss_bytecode_label_t pss_comp_last_loop_end(pss_comp_t* comp);


/**
 * @brief Expect the next token is the given type, otherwise raise a compile error
 * @param comp The compiler
 * @param token The token
 * @return status code
 **/
int pss_comp_expect_token(pss_comp_t* comp, pss_comp_lex_token_type_t token);

/**
 * @biref Expect the next token is the give keyword otherwise raise a compile error
 * @param comp The compiler
 * @param keyword The keyword expected
 * @return status code
 **/
int pss_comp_expect_keyword(pss_comp_t* comp, pss_comp_lex_keyword_t keyword);


/**
 * @brief raise an compiler error with the given message, the line number
 *        will be automatically set to current value
 * @param comp The compiler instance
 * @param msg The message
 * @return status code
 **/
int pss_comp_raise(pss_comp_t* comp, const char* msg, ...)
	__attribute__((format(printf,2,3)));

/**
 * @brief raise an internal error and return an error code
 * @param comp The compiler
 * @param reason The reason for why we are raising
 * @return error code
 **/
int pss_comp_raise_internal(pss_comp_t* comp, pss_comp_internal_t reason);

#endif
