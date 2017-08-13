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
 * @brief The command line option data when we are parsing a command line option
 **/
typedef struct {
	const pstd_option_t*   option_array;   /*!< The list of all the options */
	const pstd_option_t*   current_option; /*!< The current option we are talking about */
	pstd_option_param_t*   param_array;    /*!< The array that contains the parameter for this option */
	uint32_t               option_array_size; /*!< The size of the option array */
	uint32_t               param_array_size;  /*!< The size of the parameter array */
	void*                  cb_data;           /*!< The additional callback function data */
} pstd_option_data_t;

/**
 * @brief the callback function used to handle the option
 * @param data the option data
 * @return status code
 **/
typedef int (*pstd_option_handler_t)(pstd_option_data_t data);

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
 * @param data The option data
 * @return status code
 **/
int pstd_option_handler_print_help(pstd_option_data_t data);

/**
 * @brief sort the options in alphabetical order
 * @param options the options array
 * @param n the size of the array
 * @return status code
 **/
int pstd_option_sort(pstd_option_t* options, uint32_t n);

#endif /* __PSTD_ARGS_H__ */
