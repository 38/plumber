/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <utils/log.h>
#include <error.h>

#include <plumber.h>

#include <plumberapi.h>

static inline plumberapi_graph_t* _to_external_graph(lang_service_t* from)
{
	union {
		plumberapi_graph_t* ex;
		lang_service_t*     in;
	} buf;

	buf.in = from;

	return buf.ex;
}

static inline lang_service_t* _to_internal_graph(plumberapi_graph_t* from)
{
	union {
		plumberapi_graph_t* ex;
		lang_service_t*     in;
	} buf;

	buf.ex = from;

	return buf.in;
}

int plumberapi_init(void)
{
	return plumber_init();
}

int plumberapi_finalize(void)
{
	return plumber_finalize();
}

int plumberapi_insmod(char const* const* mod_init_str)
{
	if(NULL == mod_init_str)
	    ERROR_RETURN_LOG(int, "Invalid arguments: mod_init_str is NULL");

	const char* binary_name = mod_init_str[0];

	const itc_module_t* binary = itc_binary_search_module(binary_name);

	if(NULL == binary)
	    ERROR_RETURN_LOG(int, "No module named %s found", binary_name);

	uint32_t argc = 0;
	for(;mod_init_str[argc + 1] != NULL; argc ++);

	if(ERROR_CODE(int) == itc_modtab_insmod(binary, argc, mod_init_str + 1))
	    ERROR_RETURN_LOG(int, "Cannot install the IO module");

	return 0;
}

plumberapi_graph_t* plumberapi_graph_new(void)
{
	return _to_external_graph(lang_service_new());
}

int plumberapi_graph_free(plumberapi_graph_t* graph)
{
	return lang_service_free(_to_internal_graph(graph));
}

plumberapi_node_t plumberapi_graph_add_node(plumberapi_graph_t* graph, char const* node_init_str)
{
	if(NULL == graph || NULL == node_init_str)
	    ERROR_RETURN_LOG(plumberapi_node_t, "Invalid arguments");

	return (plumberapi_node_t)lang_service_add_node(_to_internal_graph(graph), node_init_str);
}

int plumberapi_get_num_config(const char* key, int64_t* val)
{
	if(NULL == key || NULL == val)
	    ERROR_RETURN_LOG(int, "Invalid arguments");
	lang_prop_value_t ret = lang_prop_get(key);

	if(ret.type == LANG_PROP_TYPE_ERROR)
	    ERROR_RETURN_LOG(int, "Configuration Setting Failure: key = %s", key);

	if(ret.type == LANG_PROP_TYPE_INTEGER)
	{
		*val = ret.num;
		return 1;
	}

	return 0;
}

int plumberapi_get_str_config(const char* key, char ** val)
{
	if(NULL == key || NULL == val)
	    ERROR_RETURN_LOG(int, "Invalid arguments");
	lang_prop_value_t ret = lang_prop_get(key);

	if(ret.type == LANG_PROP_TYPE_ERROR)
	    ERROR_RETURN_LOG(int, "Configuration Setting Failure: key = %s", key);

	if(ret.type == LANG_PROP_TYPE_STRING)
	{
		*val = ret.str;
		return 1;
	}

	return 0;
}

int plumberapi_set_str_config(const char* key, const char* val)
{
	if(NULL == key || NULL == val)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	lang_prop_value_t newval = {
		.type = LANG_PROP_TYPE_STRING,
		.str  = strdup(val)
	};

	if(newval.str == NULL)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the string value");

	int rc = lang_prop_set(key, newval);

	free(newval.str);

	return rc;
}

int plumberapi_set_num_config(const char* key, int64_t val)
{
	if(NULL == key)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	lang_prop_value_t newval = {
		.type = LANG_PROP_TYPE_INTEGER,
		.num  = val
	};

	if(newval.str == NULL)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the string value");

	int rc = lang_prop_set(key, newval);

	free(newval.str);

	return rc;
}


int plumberapi_graph_source(plumberapi_graph_t* graph, plumberapi_node_t node, char const* port)
{
	if(NULL == graph || ERROR_CODE(plumberapi_node_t) == node || NULL == port)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return lang_service_set_input(_to_internal_graph(graph), node, port);
}

int plumberapi_graph_sink(plumberapi_graph_t* graph, plumberapi_node_t node, char const* port)
{
	if(NULL == graph || ERROR_CODE(plumberapi_node_t) == node || NULL == port)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return lang_service_set_output(_to_internal_graph(graph), node, port);
}

static void _stop(int signo)
{
	(void)signo;
	sched_loop_kill(1);
}

int plumberapi_graph_run(plumberapi_graph_t* graph)
{
	signal(SIGINT, _stop);
	return lang_service_start(_to_internal_graph(graph), 0);
}

int plumberapi_graph_connect(plumberapi_graph_t* graph,
                             plumberapi_node_t from_node, char const* from_port,
                             plumberapi_node_t to_node,   char const* to_port)
{
	if(NULL == graph ||
	   ERROR_CODE(plumberapi_node_t) == from_node || NULL == from_port ||
	   ERROR_CODE(plumberapi_node_t) == to_node   || NULL == to_port)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return lang_service_add_edge(_to_internal_graph(graph),
	                             from_node, from_port,
	                             to_node,   to_port);
}

const char* plumberapi_graph_port_type(plumberapi_graph_t* graph, plumberapi_node_t node, char const* port)
{
	if(NULL == graph || ERROR_CODE(plumberapi_node_t) == node || NULL == port)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	return lang_service_get_type(_to_internal_graph(graph), node, port);
}

char** plumberapi_graph_node_ports(plumberapi_graph_t* graph, plumberapi_node_t node)
{
	return lang_service_node_port_names(_to_internal_graph(graph), node);
}

int plumberapi_portlist_free(char** list)
{
	char** list_head = list;
	for(; NULL != list && NULL != *list; list ++)
	    free(*list);

	free(list_head);

	return 0;
}

int plumberapi_update_graph(plumberapi_graph_constructor_func_t cons, void* data)
{
	if(NULL == cons)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == runtime_stab_switch_namespace())
	    ERROR_RETURN_LOG(int, "Cannot swtich current namespace");

	lang_service_t* serv = _to_internal_graph(cons(data));

	if(NULL == serv)
	    ERROR_LOG_GOTO(ERR, "Cannot create the new dataflow graph");

	if(ERROR_CODE(int) == lang_service_start_deployment(serv))
	    ERROR_LOG_GOTO(ERR, "Cannot start the update process");

	while(!sched_loop_deploy_completed())
	{
		LOG_DEBUG("Graph update in progress");
		usleep(100000);
	}

	if(ERROR_CODE(int) == runtime_stab_dispose_unused_namespace())
	    ERROR_LOG_GOTO(ERR, "Cannot dispose the previous namespace");

	return 0;

ERR:
	runtime_stab_revert_current_namespace();

	return ERROR_CODE(int);
}
