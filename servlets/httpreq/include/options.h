/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief define the HTTP parser options
 * @file httpreq/include/options.h
 **/
#ifndef __HTTPREQ_OPTIONS_H__
#define __HTTPREQ_OPTIONS_H__

/**
 * @brief the options that parsed from the servlet param
 **/
typedef struct {
	uint32_t method_allowed;                    /*!< which verb it should support */
	uint32_t produce_method:1;                  /*!< if we want to produce HTTP verb */
	uint32_t produce_host:1;                    /*!< if we want to produce host name */
	uint32_t produce_path:1;                    /*!< if we want to produce the path */
	uint32_t produce_cookie:1;                  /*!< if we want to produce the cookie */
	uint32_t text_output:1;                     /*!< if this flag is true, we will output plain text output instead the bianry output */
} httpreq_options_t;

/**
 * @brief parse the option from the command line
 * @param argc the number of arguments
 * @param argv the argument array
 * @return the newly create options, NULL indicates error
 **/
httpreq_options_t* httpreq_options_parse(uint32_t argc, char const * const * argv);

/**
 * @brief dispose the http parser options
 * @param options the http request pareser options object
 * @return status code
 **/
int httpreq_options_free(httpreq_options_t* options);

#endif /* __HTTPREQ_OPTIONS_H__ */
