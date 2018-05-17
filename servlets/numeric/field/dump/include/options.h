/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The servlet options for the field dumping servlet
 * @file field/dump/include/options.h
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The output format
 **/
typedef enum {
	OPTIONS_FORMAT_BINARY,    /*!< Dump the field in binary format */
	OPTIONS_FORMAT_TEXT       /*!< Dump the field in text format */
} options_format_t;

/**
 * @brief The servlet options 
 **/
typedef struct {
	uint32_t    dump_dim:1;       /*!< If we need to dump the dimensional data */
	uint32_t    slice_coord:1;    /*!< If we need to dump the slice coordinate */
	uint32_t    binary:1;         /*!< If we should dump this to a binary format */
#if 0
	uint32_t    raw:1;            /*!< TODO: If we should dump this to a RAW format, currently we dump everything into RAW format */
#endif
} options_t;

/**
 * @brief Parse the servlet intialization options
 * @param argc The number of arguments
 * @param argv The servlet init argument array
 * @param buf The options buffer
 * @return status code
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buf);

/**
 * @brief dispose the used servlet options
 * @param opt the option to dispose
 * @note This function will assume the option object itself belongs to another struct. 
 *       It just dispose the internal objects
 * @return status code
 **/
int options_free(options_t* buf);

#endif /* __OPTIONS_H__ */
