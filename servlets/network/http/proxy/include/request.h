/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The RLS object for a proxy HTTP request
 * @file proxy/include/request.h
 **/

#ifndef __REQUEST_H__
#define __REQUEST_H__
/**
 * @brief The method of a HTTP request
 **/
typedef enum {
	REQUEST_METHOD_GET,    /*!< The HTTP GET method */
	REQUEST_METHOD_POST,   /*!< The HTTP POST method */
	REQUEST_METHOD_HEAD,   /*!< The HTTP HEAD method */
	REQUEST_METHOD_PUT,    /*!< The HTTP PUT method */
	REQUEST_METHOD_DELETE  /*!< The HTTP DELETE method */
} request_method_t;

/**
 * @brief The request parameter
 **/
typedef struct {
	request_method_t method;         /*!< The request method */
	const char*      host;           /*!< The host name */
	const char*      base_dir;       /*!< The base directory */
	const char*      relative_path;  /*!< The relative path */
	const char*      query_param;    /*!< The query parameter */
	const char*      content;        /*!< The data body */
	size_t           content_len;    /*!< The content length */

	uint64_t         range_begin;    /*!< The start point of the range */
	uint64_t         range_end;      /*!< The end point of range */
} request_param_t;

/**
 * @brief The declaration for a RLS proxy object
 **/
typedef struct _request_t request_t;

/**
 * @brief Create a new request RLS proxy
 * @param param The request parameters
 * @param timeout The time limit for connection wait
 * @return status code
 **/
request_t* request_new(const request_param_t* param, uint32_t timeout);

/**
 * @brief Dispose a uncommited request
 * @param req The request
 * @return status code
 **/
int request_free(request_t* req);

/**
 * @brief Commit the request to the RLS scope
 * @param request The request object
 * @return The token that is used to refer the object
 **/
scope_token_t request_commit(request_t* request);

#endif
