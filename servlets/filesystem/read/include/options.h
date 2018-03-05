/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The servlet options for filesystem/read
 * @file filesystem/read/include/options.h
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The servlet options
 **/
typedef struct {
	char*     root;          /*!< The root directory for the content we can fetch */
	size_t    root_len;      /*!< The length of the root directory */
	char*     mime_spec;     /*!< The mime type specification file */
	char*     forbiden_page; /*!< The file to the customized 403 page */
	char*     not_found_page;/*!< The file for the 404 page */
	char*     compress_list; /*!< The list of mime type wildcards to compress */
	char*     dir_index_file;/*!< The list of file names that can used as the directory index */
	enum {
			  OPTIONS_OUTPUT_FILE,  /*!< Use the pstd file object */
			  OPTIONS_OUTPUT_HTTP,  /*!< Use the HTTP response object */
		      OPTIONS_OUTPUT_RAW    /*!< Use the untyped pipe for the result */
	}         output_mode;     /*!< Indicates the output type */
	uint8_t   inscure:1;     /*! The inscure mode, which allows request of file ouside of the root using .. */
} options_t;

/**
 * @brief Parse the servlet options
 * @param argc The argument count
 * @param argv The actual argument array
 * @param buf The options buffer
 * @return status code
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buf);

/**
 * @brief Dispose the used options
 * @note This function assume the options buffer is allocate statically
 * @param opt The options
 * @return status code
 **/
int options_free(const options_t* buf);

#endif
