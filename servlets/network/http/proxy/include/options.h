/**
 * Copyright (C) 2018, Hao Hou
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The servlet init string options
 **/
typedef struct {
	uint32_t  conn_pool_size;   /*!< The minimal required connection pool size */
	uint32_t  conn_per_peer;    /*!< The maximum number of connection of the same peer */
	uint32_t  conn_timeout;     /*!< The connection timeout */
} options_t;

/**
 * @brief Parse the string init option
 * @param argc The number of arguments
 * @param argv The argument list
 * @param buf The buffer for the option
 * @return status code
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buf);

#endif
