/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the Plumber Service Script compiler
 * @file lang/compiler.h
 **/
#ifndef __PLUMBER_LANG_COMPILER_H__
#define __PLUMBER_LANG_COMPILER_H__
/**
 * @brief the incomplete type for a compiler
 **/
typedef struct _lang_compiler_t lang_compiler_t;

/**
 * @brief the compiler options
 **/
typedef struct {
	uint32_t  log_bytecode;    /*!< log bytecode it produced */
	uint32_t  log_statement;   /*!< log the statement the compiler is currently working on */
	uint32_t  reg_limit;       /*!< the maximum number of register the compiler can use */
} lang_compiler_options_t;

/**
 * @brief the compiler error
 **/
typedef struct _lang_compiler_error_t {
	const char* file;   /*!< the source code file name */
	uint32_t    line;   /*!< the line number */
	uint32_t    off;    /*!< the offset in that line */
	const char* message;/*!< the compiler error message */
	struct _lang_compiler_error_t* next; /*!< the next error */
} lang_compiler_error_t;
/**
 * @brief instantiate a new compiler instance
 * @param lexer the input lexer
 * @param bc_table the bytecode table
 * @param options the compiler options
 * @return the newly created compiler instance
 **/
lang_compiler_t* lang_compiler_new(lang_lex_t* lexer, lang_bytecode_table_t* bc_table, lang_compiler_options_t options);

/**
 * @brief dispose a used compiler instance
 * @note this function won't dispose the lexer and bc_table used by this compiler instance
 * @param compiler the used compiler instnace
 * @return status code
 **/
int lang_compiler_free(lang_compiler_t* compiler);

/**
 * @brief compile a series of lexer result and produce bytecode in the bytecode table
 * @param compiler the used compiler instnace
 * @return status code
 **/
int lang_compiler_compile(lang_compiler_t* compiler);

/**
 * @brief get the compiler error message
 * @param compiler the target compiler instance
 * @return status code
 **/
lang_compiler_error_t* lang_compiler_get_error(const lang_compiler_t* compiler);

/**
 * @brief check compiler status
 * @note this function is used for testing purpose to make sure that the compiler is in an expected state
 * @param compiler the target compiler
 * @return status code
 **/
int lang_compiler_validate(const lang_compiler_t* compiler);


#endif /* __PLUMBER_LANG_COMPILER_H__ */
