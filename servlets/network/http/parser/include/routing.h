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
 * @brief Describe a routing description
 **/
typedef struct {
	/* HTTPS upgrade support */
	uint32_t           upgrade_http:1;    /*!< Indicates for this part we need to upgrade http to https */
	const char*        https_url_base;    /*!< If specified, it means do not just replate the scheme string in the original URL, but use this as the base directory */

	/* URL pattern */
	const char*        url_base;    /*!< The URL prefix we need to match, don't support any wildcard */

	/* The output */
	const char*        pipe_port_name;   /*!< The port we need to output the result */
} routing_desc_t;

/**
 * @brief The accessor used for produce the output
 **/
typedef struct {
	pstd_type_accessor_t   a_method;       /*!< The accessor to the method code */
	pstd_type_accessor_t   a_rel_url;      /*!< The accessor to the relative URL */
	pstd_type_accessor_t   a_base_url;     /*!< The accessor to the base URL */
	pstd_type_accessor_t   a_host;         /*!< The accessor to the host name */
	pstd_type_accessor_t   a_query_param;  /*!< The accessor to the query param */
	pstd_type_accessor_t   a_range_begin;  /*!< The beginging of the range */
	pstd_type_accessor_t   a_range_end;    /*!< The end of the range */
	pstd_type_accessor_t   a_body;         /*!< The accessor for the body data */
} routing_output_t;

/**
 * @brief Describe the routing result that we can use
 **/
typedef struct {
	/* The URL and host related */
	const char*            url_base;       /*!< The path base for the matched rule */
	size_t                 url_base_len;   /*!< The length of the path base */
	size_t                 host_len;       /*!< The length of the host */

	/* HTTPS upgrade */
	uint32_t               should_upgrade; /*!< If we need to upgrade the protocol */
	const char*            https_url_base; /*!< If we need upgrade the protocol and we can not simply just use https://host/url_base/relative_url, this will we set */

	/* The output related */
	const routing_output_t* out;           /*!< The data structure thatt is used to describe the routing accessors */
} routing_result_t;

/**
 * @brief The routing map state
 **/
typedef struct {
	const routing_map_t* map;         /*!< The routing map to search */
	trie_search_state_t  idx_state;   /*!< The index state */
	routing_result_t*    result_buf;  /*!< The result buffer */
	uint32_t             done:1;      /*!< If the routing map has determined the routing */
} routing_state_t;

/**
 * @brief Initialize a routing map state variable that is used for routing
 * @param state The state to initailize
 * @param result_buf The result buffer
 * @return nothing
 **/
static inline void routing_state_init(routing_state_t* state, const routing_map_t* map, routing_result_t* result_buf)
{
	trie_state_init(&state->idx_state);
	state->result_buf = result_buf;
	state->done = 0;
	state->map = map;
}


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
 * @brief Process the next buffer that contains the URL information
 * @param map   The routing map
 * @param state The routing state
 * @param buf The data buffer
 * @param buf_len The length of the buffer
 * @param last If this is the last buffer
 * @return number of bytes has been accepted or error code
 **/
size_t routing_process_buffer(routing_state_t* state, const char* buf, size_t buf_len, int last);

#endif
