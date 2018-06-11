/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The servlet options for the field parser
 * @file field/parser/include/options.h
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#include <psnl/dim.h>
#include <psnl/cpu/field.h>

/**
 * @brief The input format of the parser
 **/
typedef enum {
	OPTIONS_INPUT_FORMAT_STRING,   /*!< Indicates this is a string representation, which is a list of numbers */
	OPTIONS_INPUT_FORMAT_BINARY    /*!< Indicates this is a binary represetantion */
} options_input_format_t;

/**
 * @brief The option of this servlet
 **/
typedef struct {
	uint32_t               n_dim;                      /*!< The dimensional data */
	psnl_dim_t*            dim_data;                   /*!< An optional dimension data, if this is given it means the parser
							                            *   will assume the field size. Ohterwise the parser should determine the field
							                            *   size from the input */
	uint32_t                   raw:1;                      /*!< If Indicates we should use raw input */
	options_input_format_t     in_format;                  /*!< The input format */
	psnl_cpu_field_cell_type_t cell_type;                  /*!< The type of the cell */
	const char*                input_type;                 /*!< The type for the input port */
	char*                      result_type;                /*!< The type of the output port */
} options_t;

/**
 * @brief Parse the options
 * @param argc The number of arguments
 * @param argv The arguments list
 * @return The newly created options pointer from the argument list, NULL on error
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buf);

/**
 * @brief Dipsose a used option
 * @param opt The option to dispose
 * @return status code
 **/
int options_free(options_t* opt);

#endif /* __OPTIONS_H__ */
