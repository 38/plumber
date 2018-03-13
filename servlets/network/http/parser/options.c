/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <trie.h>
#include <routing.h>
#include <options.h>

static int _route(pstd_option_data_t data)
{
	options_t* options = (options_t*)data.cb_data;

	if(data.param_array_size < 1)
	    ERROR_RETURN_LOG(int, "Unexpected number of parameters");

	const char* param = data.param_array[0].strval;

	static char buffer[4096];
	snprintf(buffer, sizeof(buffer), "%s", param);
	char* begin;

	routing_desc_t desc = {};

	for(begin = buffer; begin;)
	{
		char* end = strchr(begin, ';');
		if(end == NULL) end = begin + strlen(begin);

		char* key = begin;

		begin = *end == 0 ? NULL : end + 1;
		if(*end) *end = 0;

		char* val = strchr(key, ':');
		if(NULL != val) *(val ++) = 0;

		if(strcmp(key, "name") == 0)
			desc.pipe_port_name = val;
		else if(strcmp(key, "prefix") == 0)
			desc.url_base = val;
		else if(strcmp(key, "upgrade_http") == 0)
		{
			desc.upgrade_http = 1;
			desc.https_url_base = val;
		}
	}

	if(desc.pipe_port_name == NULL || desc.url_base == NULL)
		ERROR_RETURN_LOG(int, "Malformed route description");

	if(ERROR_CODE(int) == routing_map_add_routing_rule(options->routing_map, desc))
		ERROR_RETURN_LOG(int, "Cannot add the routing rule to the routing map");

	return 0;
}

static pstd_option_t _options[] = {
	{
		.short_opt    = 'h',
		.long_opt     = "help",
		.description  = "Show this help message",
		.pattern      = "",
		.handler      = pstd_option_handler_print_help
	},
	{
		.short_opt    = 'r',
		.long_opt     = "route",
		.description  = "Add a routing rule: Format --route name:<pipe_name>;prefix:<prefix>[;upgrade_http[:https_url_base]]",
		.pattern      = "S",
		.handler      = _route
	}
};

int options_parse(uint32_t argc, char const* const* argv, options_t* buf)
{
	if(NULL == argv || NULL == buf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	memset(buf, 0, sizeof(*buf));

	if(NULL == (buf->routing_map = routing_map_new()))
		ERROR_RETURN_LOG(int, "Cannot create routing map");

	
	if(ERROR_CODE(int) == pstd_option_sort(_options, sizeof(_options) / sizeof(_options[0])))
	    ERROR_RETURN_LOG(int, "Cannot sort the opts array");

	if(ERROR_CODE(uint32_t) == pstd_option_parse(_options, sizeof(_options) / sizeof(_options[0]), argc, argv, buf))
	    ERROR_RETURN_LOG(int, "Cannot parse the servlet initialization string");

	return 0;
}

int options_free(const options_t* options)
{
	if(NULL == options)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != options->routing_map)
		return routing_map_free(options->routing_map);

	return 0;
}
