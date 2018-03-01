/**
 * Copyright (C) 2018, Hao Hou
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The options that we used for this servlet
 **/
typedef struct {
	uint32_t    gzip:1;      /*!< If we need to support gzip encoding */
	uint32_t    compress:1;  /*!< If we need to support LZW encoding */
	uint32_t    deflate:1;   /*!< If we need to support deflate encoding */
	uint32_t    br:1;        /*!< If we need to support BR encoding */
	uint32_t    chuncked:1;  /*!< If we need to support chuncked transfer encoding */
	/* TODO: for some of the mime type we actually don't want to compress */
} options_t;

/**
 * @brief Parse the servlet init string
 * @param argc The argument count
 * @param argv The actual argument array
 * @param buffer The buffer for the options
 * @return status code
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buffer);

#endif /* __OPTIONS_H__ */
