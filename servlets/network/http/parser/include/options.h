/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The options for the HTTP parser
 * @file network/http/parser/include/options.h
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The actual servlet options. The routing can be described as --routing name:static_content;prefix:plumberserver.com/static/;upgrade_http ...
 **/
typedef struct {
	routing_map_t*          routing_map;        /*!< The HTTP routing map */
} options_t;

/**
 * @brief Parse the servlet init string
 * @param argc The number of servlet init string param
 * @param argv The list of servlet init string sections
 * @param buf The options buffer
 * @return status code
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buf);

/**
 * @brief Dipsoe the options
 * @note This will assume that the options is statically allocated
 * @return status code
 **/
int options_free(const options_t* options);

#endif /* __OPTIONS_H__ */
