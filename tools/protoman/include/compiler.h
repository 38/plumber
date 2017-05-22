/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The protocol type description langauge compiler
 * @file protoman/include/compiler.h
 **/

#ifndef __PROTOMAN_COMPILER_H__
#define __PROTOMAN_COMPILER_H__

/**
 * @brief represent a compiler compiled protocol type
 **/
typedef struct _compiler_type_t {
	char          name[128];  /*!< the name for this type */
	const char*   package;    /*!< the package name of this protocol */
	const char*   source;     /*!< the source code filename */
	proto_type_t* proto_type; /*!< the protocol type description data */
	struct _compiler_type_t* next; /*!< the next pointer in the compiler type list */
} compiler_type_t;

/**
 * @brief the type that is used as the compiling result from a type description language source code
 **/
typedef struct {
	char*                filename;   /*!< the source code filename */
	proto_ref_typeref_t* package;    /*!< the package name of this protocol */
	compiler_type_t*     type_list;  /*!< the linked list for all the compiled types from this file */
} compiler_result_t;

/**
 * @brief the compiler options
 **/
typedef struct {
	uint32_t   padding_size;    /*!< the padding size the compiler should use */
	lexer_t*   lexer;           /*!< the lexer we used for complation */
} compiler_options_t;

/**
 * @brief performance the actual compilation
 * @param options the compiler options
 * @return the compiler result NULL if any error found
 **/
compiler_result_t* compiler_compile(compiler_options_t options);

/**
 * @brief dispose the compiler
 * @param result the result used compiler result
 * @return status code
 **/
int compiler_result_free(compiler_result_t* result);

#endif /* __PROTOMAN_COMPILER_H__ */
