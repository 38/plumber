/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The option parser for the client servlet
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The servlet configurations
 **/
typedef struct {
	uint32_t                num_threads;      /*!< The number of threads  this servlet requested for */
	uint32_t                num_parallel;     /*!< The number parallel requests the client can do */
	uint32_t                queue_size;       /*!< The size of the request queue */
	uint32_t                save_header:1;    /*!< If we need to save the header or metadata */
	uint32_t                follow_redir:1;   /*!< If we need follow the HTTP redirect */
	uint32_t                sync_mode:1;      /*!< Indicates this servlet is running under sync mode (which do not use the curl thread pool at all )*/
	uint32_t                string:1;         /*!< Indicates this servlet should use string response rather than the object  response */
} options_t;

/**
 * @brief Parse the servlet init options 
 * @param argc The count of the arguments
 * @param argv The actual values of the arguments
 * @param buf  The buffer for the parsed options
 * @return status code
 **/
int options_parse(uint32_t argc, char const* const* argv, options_t* buf);

#endif
