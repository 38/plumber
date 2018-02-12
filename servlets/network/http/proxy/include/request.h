/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The RLS object for a proxy HTTP request
 * @file proxy/include/request.h
 **/

#ifndef __REQUEST_H__
#define __REEUEST_H__
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
 * @brief The declaration for a RLS proxy object
 **/
typedef struct _request_t request_t;

/**
 * @brief Create a new request RLS proxy
 * @param method The request method
 * @param url The url for the request
 * @param data The addtional payload data
 * @return status code
 **/
request_t* request_new(request_method_t method, const char* url, const char* data, size_t data_len);

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
