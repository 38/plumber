/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <routing.h>

/**
 * @brief The actual pipe and type layout for a rule
 **/
typedef struct {
	uint32_t               upgrade_http:1; /*!< If we need to upgrade the HTTP to HTTPS protocol */
	char*                  https_url_base; /*!< If specified it means we should redirect the to another URL */
	
	pipe_t                 p_out;          /*!< The output pipe */
	pstd_type_accessor_t   a_method;       /*!< The accessor to the method  */
	pstd_type_accessor_t   a_rel_url;      /*!< The accessor to the relative URL */
	pstd_type_accessor_t   a_base_url;     /*!< The accessor to the base URL */
	pstd_type_accessor_t   a_host;         /*!< The accessor to the host name */
	pstd_type_accessor_t   a_query_param;  /*!< The accessor to the query param */
	pstd_type_accessor_t   a_range_begin;  /*!< The beginging of the range */
	pstd_type_accessor_t   a_range_end;    /*!< The end of the range */
} _rule_data_t;

/**
 * @brief The actual data structure for a routing rule
 **/
typedef struct {
	uint64_t               hash[2];        /*!< The hash code for this rule */
	uint64_t               hash_mask;      /*!< The hash mask used to identify if this matches */
	char*                  host_name;      /*!< The host name */
	size_t                 host_name_len;  /*!< The length of the host name */
	char*                  url_prefix;     /*!< The URL prefix */
	size_t                 url_prefix_len; /*!< The length of the URL prefix */
	routing_protocol_t     method;         /*!< The method we can match */
	_rule_data_t           data;           /*!< The rule data */
} _rule_t;

/**
 * @brief The actual data structure for a routing map 
 **/
struct _routing_map_t {
	uint32_t init:1;                       /*!< Indicates if this map has been completely initialized, which means we have createtd a map and set type model */
	uint16_t n_rules;                      /*!< The number of rules */
	uint16_t cap_rules;                    /*!< The capacity of the rules array */
	_rule_t* rules;                        /*!< The rules table */
	uint8_t* short_hash;                   /*!< The shorter hash table */
	_rule_data_t  default_rule;            /*!< The default rule */
};

static inline void _rule_hash(const char* host, size_t host_len, const char* url, size_t url_len, uint64_t* buf)
{
	buf[0] = buf[1] = 0;
	
	memcpy(buf, host, host_len < sizeof(uint64_t) ? host_len : sizeof(uint64_t));
	memcpy(buf, url, url_len < sizeof(uint64_t) ? url_len : sizeof(uint64_t));

	buf[0] = __builtin_bswap64(buf[0]);
	buf[1] = __builtin_bswap64(buf[1]);
}

static uint8_t _short_hash(const uint64_t* hash)
{
	return (uint8_t)(hash[0] % 253);
}

routing_map_t* routing_map_new()
{
	routing_map_t* ret = (routing_map_t*)malloc(sizeof(routing_map_t));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocatae memory for the routing map");
	
	ret->cap_rules = 32;

	if(NULL == (ret->rules = (_rule_t*)malloc(sizeof(_rule_t) * ret->cap_rules)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the rule array");

	return ret;
ERR:
	free(ret);
	return NULL;
}

int routing_map_free(routing_map_t* map)
{
	if(NULL == map)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(map->rules != NULL)
	{
		uint32_t i;
		for(i = 0; i < map->n_rules; i ++)
		{
			if(NULL != map->rules[i].host_name)
				free(map->rules[i].host_name);
			if(NULL != map->rules[i].url_prefix)
				free(map->rules[i].url_prefix);
			if(NULL != map->rules[i].data.https_url_base)
				free(map->rules[i].data.https_url_base);
		}
	}

	if(NULL == map->default_rule.https_url_base)
		free(map->default_rule.https_url_base);

	free(map);
	return 0;
}

int routing_map_add_routing_rule(routing_map_t* map, routing_desc_t rule)
{
	if(NULL == map || NULL == rule.host || NULL == rule.url_base || NULL == rule.port_name)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(map->init)
		ERROR_RETURN_LOG(int, "Cannot change the mapping layout of an initialized map");

	if(map->cap_rules <= map->n_rules)
	{
		LOG_DEBUG("The rule array has insufficient space, resizing");

		uint16_t new_cap = (uint16_t)(map->cap_rules * 2u);

		_rule_t* new_arr = (_rule_t*)realloc(map->rules, sizeof(_rule_t) * new_cap);
		if(NULL == new_arr)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the rule array");

		map->cap_rules = new_cap;
		map->rules = new_arr;
	}

	_rule_t* buf = map->rules + map->n_rules;
	_rule_hash(rule.host, buf->host_name_len = strlen(rule.host), rule.url_base, buf->url_prefix_len = strlen(rule.url_base), buf->hash);

	buf->hash_mask = 0xffffffffffffffffull;

	if(buf->url_prefix_len < sizeof(uint64_t))
		buf->hash_mask <<= (sizeof(uint64_t) - buf->url_prefix_len) * 8;

	if(NULL == (buf->host_name = strdup(rule.host)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the host name");

	if(NULL == (buf->url_prefix = strdup(rule.url_base)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the URL prefix");

	buf->method = rule.method;
	buf->data.upgrade_http = rule.upgrade_http;

	if(NULL != (buf->data.https_url_base = strdup(rule.https_url_base)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the HTTPS url base");

	if(ERROR_CODE(pipe_t) == (buf->data.p_out = pipe_define(rule.port_name, PIPE_OUTPUT, "plumber/std_servlet/network/http/parser/v0/RequestData")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the pipe for routing %s", rule.port_name);

	map->n_rules ++;
	return 0;
ERR:
	if(NULL != buf->host_name) free(buf->host_name);
	if(NULL != buf->url_prefix) free(buf->url_prefix);
	if(NULL != buf->data.https_url_base) free(buf->data.https_url_base);

	return ERROR_CODE(int);
}

int routing_map_set_default_http_upgrade(routing_map_t* map, uint32_t upgrade_enabled, const char* url_base)
{
	if(NULL == map)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	char* new_url = NULL;

	if(url_base != NULL && NULL == (new_url = strdup(url_base)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the URL base");

	if(NULL != map->default_rule.https_url_base)
		free(map->default_rule.https_url_base);

	map->default_rule.https_url_base = new_url;

	map->default_rule.upgrade_http = (upgrade_enabled != 0);

	return 0;
}

static inline int _init_rule_data(_rule_data_t* rd, pstd_type_model_t* type_model)
{
	if(ERROR_CODE(pstd_type_accessor_t) == (rd->a_method = pstd_type_model_get_accessor(type_model, rd->p_out, "method")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for method");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->a_rel_url = pstd_type_model_get_accessor(type_model, rd->p_out, "relative_url")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for relative URL");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->a_base_url = pstd_type_model_get_accessor(type_model, rd->p_out, "base_url")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for base URL");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->a_host = pstd_type_model_get_accessor(type_model, rd->p_out, "host")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for host name");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->a_query_param = pstd_type_model_get_accessor(type_model, rd->p_out, "query_param")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for query param");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->a_range_begin = pstd_type_model_get_accessor(type_model, rd->p_out, "range_begin")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for the range begin");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->a_range_end = pstd_type_model_get_accessor(type_model, rd->p_out, "range_end")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for the range end");

	return 0;
}

static inline int _int64_cmp(uint64_t a, uint64_t b)
{
	if(a > b) return 1;
	if(a < b) return -1;
	return 0;
}

static inline int _rule_comp(const void* pa, const void* pb)
{
	const _rule_t* a = (const _rule_t*)pa;
	const _rule_t* b = (const _rule_t*)pb;

	int ret = _int64_cmp(a->hash[0], b->hash[0]);
	if(ret) return ret;

	return _int64_cmp(a->hash[1], b->hash[1]);
}

int routing_map_initialize(routing_map_t* map, pstd_type_model_t* type_model)
{
	if(NULL == map || NULL == type_model)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	uint16_t i;
	for(i = 0; i < map->n_rules; i ++)
	{
		_rule_data_t* rd = &map->rules[i].data;
		if(ERROR_CODE(int) == _init_rule_data(rd, type_model))
			ERROR_RETURN_LOG(int, "Cannot initialize the rule data");
	}

	if(ERROR_CODE(int) == _init_rule_data(&map->default_rule, type_model))
		ERROR_RETURN_LOG(int, "Cannot initialize the rule data");

	qsort(map->rules, map->n_rules, sizeof(_rule_t), _rule_comp);

	/* TODO: Actually, this is not a effecient index, think about if there's any
	 * better way for indexing */

	if(NULL == (map->short_hash = (uint8_t*)malloc(sizeof(uint8_t) * map->n_rules)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the short hash");

	for(i = 0; i < map->n_rules; i ++)
		map->short_hash[i] = _short_hash(map->rules[i].hash);

	return 0;
}

int routing_map_match_request(const routing_map_t* map, 
		                      routing_method_t method, routing_protocol_t scheme, 
							  const char* host, size_t host_len,
							  const char* url,  size_t url_len, routing_result_t* resbuf)
{
	if(NULL == map || NULL == host || NULL == url || NULL == resbuf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	uint64_t hash[2];
	_rule_hash(host, host_len, url, url_len, hash);

	uint8_t short_hash = _short_hash(hash);
	
	const _rule_data_t* rule_data = &map->default_rule;
	size_t prefix_len = 0;

	uint32_t i;
	for(i = 0; i < map->n_rules; i ++)
		if(short_hash == map->short_hash[i])
		{
			if(map->rules[i].hash[0] > hash[0]) break;
			if(map->rules[i].hash[0] == hash[0] && (method & map->rules[i].method) > 0)
			{
				if(map->rules[i].hash[1] > hash[1]) break;
		    	if(((map->rules[i].hash[1] ^ hash[1]) & map->rules[i].hash_mask) == 0 &&
		           map->rules[i].host_name_len == host_len &&
		           map->rules[i].url_prefix_len <= url_len &&
		           memcmp(map->rules[i].host_name, host, host_len) == 0 &&
				   memcmp(map->rules[i].url_prefix, url, map->rules[i].url_prefix_len) == 0)
				{
					rule_data = &map->rules[i].data;
					prefix_len = map->rules[i].url_prefix_len;
					break;
				}
			}
		}

	resbuf->host = host;
	resbuf->url_base = url;
	resbuf->relative_url = url + prefix_len;

	if(scheme == ROUTING_PROTOCOL_HTTP && rule_data->upgrade_http)
	{
		resbuf->should_upgrade = 1;
		resbuf->https_url_base = rule_data->https_url_base;
	}
	else
	{
		resbuf->should_upgrade = 0;
		resbuf->https_url_base = NULL;
	}

	resbuf->a_method = rule_data->a_method;
	resbuf->a_rel_url = rule_data->a_rel_url;
	resbuf->a_base_url = rule_data->a_base_url;
	resbuf->a_host = rule_data->a_host;
	resbuf->a_query_param = rule_data->a_query_param;
	resbuf->a_range_begin = rule_data->a_range_begin;
	resbuf->a_range_end = rule_data->a_range_end;

	return 0;
}
