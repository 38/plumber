/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the PSTD servlet argument parser
 * @file pstd/include/pstd/option.h
 **/
#ifndef __PSTD_OPTION_H__
#define __PSTD_OPTION_H__

#include <stdint.h>

/**
 * @brief the previous definition of the pstd_option_t
 **/
typedef struct _pstd_option_t pstd_option_t;

/**
 * @brief the option param type
 **/
typedef enum {
	PSTD_OPTION_TYPE_INT,
	PSTD_OPTION_TYPE_DOUBLE,
	PSTD_OPTION_STRING
} pstd_option_param_type_t;

/**
 * @brief describe an option param
 **/
typedef struct {
	pstd_option_param_type_t type;   /*!< the type of this param */
	union {
		int64_t intval;        /*!< the integer value */
		double  doubleval;     /*!< the double value */
		const char* strval;    /*!< the string value */
	};
} pstd_option_param_t;

/**
 * @brief the callback function used to handle the option
 * @param idx the index of the option that need to be handled in the pstd_option_t array
 * @param params the parameter arrary
 * @param nparam the number of params
 * @param option the option definition (this will pass the entire options array), so the current option should be options[idx]
 * @param n the number of options
 * @param args the user-defined arguments
 * @return status code
 **/
typedef int (*pstd_option_handler_t)(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* args);

/**
 * @brief describe a single option
 * @note the pattern should be written like "III" ==> expecting there are three interger <br/>
 *       For optional param use * as prefix "?III" ==> an optional param with 3 integer<br/>
 *       I = integer <br/>
 *       D = double <br/>
 *       S = string <br/>
 *       All the param should be seperated with  &lt;space&gt; or &lt;tab&gt;
 **/
struct _pstd_option_t {
	const char* long_opt;        /*!< the long option */
	const char  short_opt;       /*!< the short option */
	const char* pattern;         /*!< the value pattern */
	const char* description;     /*!< the option description */
	pstd_option_handler_t handler; /*!< the handler of this option */
	void* args;                  /*!< the additional argument passed to the handler */
};

/**
 * @brief the option parser object
 **/
typedef struct _pstd_option_parser_t pstd_option_parser_t;

/**
 * @brief parse the command line options using the given parser
 * @param options the parser used to parse the command line
 * @param n the number of valid options
 * @param argc the number of arguments
 * @param argv the argument array
 * @param userdata the user defined data
 * @return the index of first argument that has not been parsed by the parser, or error code
 **/
uint32_t pstd_option_parse(const pstd_option_t* options, uint32_t n, uint32_t argc, char const* const* argv, void* userdata);

/**
 * @brief the standard option handler that print the help message
 * @param idx the index in the options array
 * @param params the param of this option
 * @param nparams the number of parameters
 * @param options the option array
 * @param n the number of options
 * @param userdata the user-defined data
 * @return status code
 **/
int pstd_option_handler_print_help(uint32_t idx, pstd_option_param_t* params, uint32_t nparams, const pstd_option_t* options, uint32_t n, void* userdata);

/**
 * @brief sort the options in alphabetical order
 * @param options the options array
 * @param n the size of the array
 * @return status code
 **/
int pstd_option_sort(pstd_option_t* options, uint32_t n);

#endif /* __PSTD_ARGS_H__ */
