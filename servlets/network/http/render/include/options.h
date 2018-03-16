/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The options for the HTTP Response Render servlet
 * @param http/render/include/options.h
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The error page options
 **/
typedef struct {
	char*          mime_type;     /*!< The MIME type for 500 error page */
	char*          error_page;    /*!< The 500 error page */
} options_error_page_t;

/**
 * @brief The servlet options
 **/
typedef struct {
	/* Compression and Encoding */
	uint8_t       gzip_enabled:1;     /*!< If gzip compression is enabled */
	uint8_t       deflate_enabled:1;  /*!< If deflate compression is enabled */
	uint8_t       br_enabled:1;       /*!< If brotli compression is enabled */
	uint8_t       chunked_enabled:1;  /*!< If we can encode the body to a chunked one */
	uint8_t       compress_level:4;   /*!< The compression level */
	uint8_t       max_chunk_size;     /*!< The max chunk size in number of pages for a chunked encoding */

	/* Reverse Proxy */
	uint8_t       reverse_proxy:1;    /*!< If this servlet should accept reverse proxy */

	/* The Server name */
	char*         server_name;        /*!< The server name we should response */

	/* Error Pages */
	options_error_page_t err_500;     /*!< The 500 error page */
	options_error_page_t err_406;     /*!< When we cannot use chunked encoding but the size is unknown */
	options_error_page_t err_503;     /*!< When we cannot perform the reverse proxying */
	options_error_page_t err_400;     /*!< When we cannot parse the request */
} options_t;

/**
 * @brief Parse the servlet init string
 * @param argc The number of arguments
 * @param argv The actual argument
 * @param buf The option buffer
 * @return status code
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buf);

/**
 * @brief Dispose the used options
 * @note This function do not actually dispose the options pointer, but dispose all the
 *       buffer allocated by options_parse function
 * @param options The options to dispose
 * @return status code
 **/
int options_free(const options_t* options);

#endif /* __OPTIONS_H__ */
