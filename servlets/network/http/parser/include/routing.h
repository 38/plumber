/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The HTTP URL routing
 * @file network/http/parser/routhing.h
 **/
#ifndef __ROUTING_H__
#define __ROUTING_H__

/**
 * @brief The routing map, which maps HTTP request to different pipe port
 **/
typedef struct _routing_map_t routing_map_t;

/**
 * @brief The bit flags used to select which protocol we need to use
 **/
typedef enum {
	ROUTING_PROTOCOL_NONE  = 0,   /*!< Doesn't match anything */
	ROUTING_PROTOCOL_HTTP  = 1,   /*!< Matches the HTTP protocol */
	ROUTING_PROTOCOL_HTTPS = 2    /*!< Matches the HTTPS protocol */
} routing_protocol_t;

/**
 * @brief The bit flags used to specify which method should allow
 **/
typedef enum {
	ROUTING_METHOD_NONE = 0,   /*!< No method is allowed */
	ROUTING_METHOD_GET  = 1,   /*!< The GET method is allowed */
	ROUTING_METHOD_POST = 2,   /*!< The POST method is allowed */
	ROUTING_METHOD_HEAD = 4    /*!< The HEAD method is allowed */
} routing_method_t;


/**
 * @brief Describe a routing description
 **/
typedef struct {
	/* HTTPS upgrade support */
	uint32_t     upgrade_http:1;    /*!< Indicates for this part we need to upgrade http to https */
	const char*  https_url_base;    /*!< If specified, it means do not just replate the scheme string in the original URL, but use this as the base directory */

	/* URL pattern */
	routing_protocol_t scheme;      /*!< The scheme HTTP or HTTPS we should use */
	routing_method_t   method;      /*!< The method we should use for this method */
	const char*        host;        /*!< The host we can match, we only allows wildcard in the beginging:
	                                 *       *domain.com will match all the host ends with domain.com */
	const char*        url_base;    /*!< The URL prefix we need to match, don't support any wildcard */

	/* The output */
	const char*        port_name;   /*!< The port we need to output the result */
} routing_desc_t;

/**
 * @brief Describe the routing result that we can use
 **/
typedef struct {
	/* The URL and host related */
	const char*      host;         /*!< The actual host name for this routing result */
	const char*      url_base;     /*!< The URL base for the matched rule */
	const char*      relative_url; /*!< The relative URL */

	/* HTTPS upgrade */
	uint32_t         should_upgrade; /*!< If we need to upgrade the protocol */
	const char*      https_url_base; /*!< If we need upgrade the protocol and we can not simply just use https://host/url_base/relative_url, this will we set */

	/* The output related */
	pstd_type_accessor_t   a_method;       /*!< The accessor to the method code */
	pstd_type_accessor_t   a_rel_url;      /*!< The accessor to the relative URL */
	pstd_type_accessor_t   a_base_url;     /*!< The accessor to the base URL */
	pstd_type_accessor_t   a_host;         /*!< The accessor to the host name */
	pstd_type_accessor_t   a_query_param;  /*!< The accessor to the query param */
	pstd_type_accessor_t   a_range_begin;  /*!< The beginging of the range */
	pstd_type_accessor_t   a_range_end;    /*!< The end of the range */
} routing_result_t;


/**
 * @brief Create a new routing map
 * @return The newly created routing map
 **/
routing_map_t* routing_map_new(void);

/**
 * @brief Dispose a used routing map
 * @param map The routing map
 * @return status code
 **/
int routing_map_free(routing_map_t* map);

/**
 * @brief Initialize the routing map
 * @note This function will finally get the accessors and constants from the type model. 
 *       This call is the last step before we can actually use the routing map
 * @param map The routing map
 * @param param The intialization param
 * @return status code
 **/
int routing_map_initialize(routing_map_t* map, pstd_type_model_t* type_model);

/**
 * @brief Add a new routing rule to the routing map
 * @param map The routing map we need to add
 * @param rule The routing rule
 * @return status code
 **/
int routing_map_add_routing_rule(routing_map_t* map, routing_desc_t rule);

/**
 * @brief Indicates if we need to upgrade HTTP to HTTPS for the default routing
 * @param map The routing map
 * @param upgrade_enabled if we need to enable the http upgrade
 * @param url_base If this is not NULL, instead of changing the url from http://... to https://..., it use the
 *                 given URL prefix
 * @return status code
 **/
int routing_map_set_default_http_upgrade(routing_map_t* map, uint32_t upgrade_enabled, const char* url_base);

/**
 * @brief Match a request in the routing table
 * @param map The routing map we use
 * @param method The HTTP method
 * @param scheme The scheme for this request either HTTP or HTTPS
 * @param host The host name 
 * @param url The URL
 * @param the matching result
 * @return status code
 **/
int routing_map_match_request(const routing_map_t* map, 
		                      routing_method_t method, routing_protocol_t scheme, 
							  const char* host, size_t host_len,
							  const char* url,  size_t url_len, routing_result_t* resbuf);

#endif
