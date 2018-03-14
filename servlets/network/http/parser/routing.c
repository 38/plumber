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

#include <trie.h>
#include <routing.h>

/**
 * @brief The actual pipe and type layout for a rule
 **/
typedef struct {
	uint32_t               upgrade_http:1; /*!< If we need to upgrade the HTTP to HTTPS protocol */
	char*                  https_url_base; /*!< If specified it means we should redirect the to another URL */
	
	pipe_t                 p_out;          /*!< The output pipe */
	routing_output_t       accessors;      /*!< The accessors */
} _rule_data_t;

/**
 * @brief The actual data structure for a routing rule
 **/
typedef struct {
	char*                  url_prefix;     /*!< The URL prefix */
	size_t                 host_len;       /*!< The length of host name in the the URL prefix */
	size_t                 url_prefix_len; /*!< The length of the URL prefix */
	_rule_data_t           data;           /*!< The rule data */
} _rule_t;

/**
 * @brief The actual data structure for a routing map 
 **/
struct _routing_map_t {
	uint16_t      n_rules;                      /*!< The number of rules */
	uint16_t      cap_rules;                    /*!< The capacity of the rules array */
	_rule_t*      rules;                        /*!< The rules table */
	_rule_data_t  default_rule;                 /*!< The default rule */
	trie_t*       index;                        /*!< The index for fast access */
};

routing_map_t* routing_map_new()
{
	routing_map_t* ret = (routing_map_t*)malloc(sizeof(routing_map_t));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocatae memory for the routing map");
	
	ret->cap_rules = 32;
	ret->n_rules = 0;
	ret->index = NULL;
	ret->default_rule.https_url_base = NULL;

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
			if(NULL != map->rules[i].url_prefix)
				free(map->rules[i].url_prefix);
			if(NULL != map->rules[i].data.https_url_base)
				free(map->rules[i].data.https_url_base);
		}

		free(map->rules);
	}

	if(NULL != map->default_rule.https_url_base)
		free(map->default_rule.https_url_base);

	int rc = 0;

	if(NULL != map->index && ERROR_CODE(int) == trie_free(map->index))
		rc = ERROR_CODE(int);

	free(map);
	return rc;
}

int routing_map_add_routing_rule(routing_map_t* map, routing_desc_t rule)
{
	if(NULL == map || NULL == rule.url_base || NULL == rule.pipe_port_name)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != map->index)
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

	if(NULL == (buf->url_prefix = strdup(rule.url_base)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the URL prefix");

	for(buf->host_len = 0; buf->url_prefix[buf->host_len] && buf->url_prefix[buf->host_len] != '/'; buf->host_len ++);

	buf->data.upgrade_http = rule.upgrade_http;

	buf->data.https_url_base = NULL;

	if(rule.https_url_base != NULL && NULL == (buf->data.https_url_base = strdup(rule.https_url_base)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the HTTPS url base");

	if(ERROR_CODE(pipe_t) == (buf->data.p_out = pipe_define(rule.pipe_port_name, PIPE_OUTPUT, "plumber/std_servlet/network/http/parser/v0/RequestData")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the pipe for routing %s", rule.pipe_port_name);

	map->n_rules ++;
	return 0;
ERR:
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
	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_method = pstd_type_model_get_accessor(type_model, rd->p_out, "method")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for method");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_rel_url = pstd_type_model_get_accessor(type_model, rd->p_out, "relative_url.token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for relative URL");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_base_url = pstd_type_model_get_accessor(type_model, rd->p_out, "base_url.token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for base URL");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_host = pstd_type_model_get_accessor(type_model, rd->p_out, "host.token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for host name");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_query_param = pstd_type_model_get_accessor(type_model, rd->p_out, "query_param.token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for query param");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_range_begin = pstd_type_model_get_accessor(type_model, rd->p_out, "range_begin")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for the range begin");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_range_end = pstd_type_model_get_accessor(type_model, rd->p_out, "range_end")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for the range end");

	if(ERROR_CODE(pstd_type_accessor_t) == (rd->accessors.a_body = pstd_type_model_get_accessor(type_model, rd->p_out, "body.token")))
		ERROR_RETURN_LOG(int, "Cannot get the type accessor for body");

	return 0;
}

int routing_map_initialize(routing_map_t* map, pstd_type_model_t* type_model)
{
	if(NULL == map || NULL == type_model)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(map->index != NULL)
		ERROR_RETURN_LOG(int, "Cannot initialize the routing map twice");

	uint16_t i;
	for(i = 0; i < map->n_rules; i ++)
	{
		_rule_data_t* rd = &map->rules[i].data;
		if(ERROR_CODE(int) == _init_rule_data(rd, type_model))
			ERROR_RETURN_LOG(int, "Cannot initialize the rule data");
	}

	if(ERROR_CODE(pipe_t) == (map->default_rule.p_out = pipe_define("default", PIPE_OUTPUT, "plumber/std_servlet/network/http/parser/v0/RequestData")))
		ERROR_RETURN_LOG(int, "Cannot create the default routing");

	if(ERROR_CODE(int) == _init_rule_data(&map->default_rule, type_model))
		ERROR_RETURN_LOG(int, "Cannot initialize the rule data");

	trie_kv_pair_t* index_buf = (trie_kv_pair_t*)malloc(sizeof(trie_kv_pair_t) * map->n_rules);
	if(NULL == index_buf)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the index buffer");

	for(i = 0; i < map->n_rules; i ++)
		index_buf[i].key = map->rules[i].url_prefix,
		index_buf[i].val = map->rules + i;

	if(NULL == (map->index = trie_new(index_buf, map->n_rules)))
		ERROR_LOG_GOTO(ERR, "Cannot create index for the routing map");

	free(index_buf);

	return 0;
ERR:
	free(index_buf);
	return ERROR_CODE(int);
}

static inline void _fill_routing_result(routing_result_t* resbuf, const _rule_data_t* rule_data, const _rule_t* rule)
{
	if(NULL == rule)
	{
		resbuf->url_base = "";
		resbuf->url_base_len = 0;
		resbuf->host_len = 0;
	}
	else
	{
		resbuf->url_base = rule->url_prefix;
		resbuf->url_base_len = rule->url_prefix_len;
		resbuf->host_len = rule->host_len;
	}

	resbuf->should_upgrade = 1;
	resbuf->https_url_base = rule_data->https_url_base;
	resbuf->out = &rule_data->accessors;
}

size_t routing_process_buffer(routing_state_t* state, const char* buf, size_t buf_len)
{
	if(NULL == state || NULL == state->map || NULL == state->result_buf || NULL == buf || 0 == buf_len || ERROR_CODE(size_t) == buf_len)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	_rule_t const* rule = NULL;
	size_t match_rc = trie_search(state->map->index, &state->idx_state, buf, buf_len, (void const**)&rule);

	if(match_rc == ERROR_CODE(size_t))
		ERROR_RETURN_LOG(size_t, "Cannot process the next buffer");

	if(match_rc == 0)
	{
		_fill_routing_result(state->result_buf, &state->map->default_rule, NULL);
		state->done = 1;
		return 0;
	}

	if(NULL != rule) 
	{
		_fill_routing_result(state->result_buf, &rule->data, rule);
		state->done = 1;
		return match_rc;
	}

	return match_rc;
}
