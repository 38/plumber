/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The Plumber Embeded API. This library provides the APIs that allows developer use Plumber as
 *        an embeded dataflow execution engine
 * @file include/plumberapi.h
 **/

#ifndef __PLUMBERAPI_H__
#define __PLUMBERAPI_H__

#include <runtime/api.h>

/**
 * @brief The dataflow object
 **/
typedef struct plumberapi_graph_t plumberapi_graph_t;

/**
 * @brief Represent a node in the dataflow graph
 * @todo Since in the code we actually use sched_service_node_id_t, which is just for the
 *       node representation. But it's not exposed as an API, so we only can do this.
 *       Think about any workaround so that we can make the code clean
 **/
typedef uint32_t plumberapi_node_t;

/**
 * @brief Initailize the Plumber Framework
 * @return status code
 **/
int plumberapi_init(void);

/**
 * @brief Finalize the Plumber Framework
 * @return status code
 **/
int plumberapi_finalize(void);

/**
 * @brief Install an IO module to the system
 * @brief mod_init_str The initialization string of the module, NULL terminated array of string
 * @return status code
 **/
int plumberapi_insmod(char const* const* mod_init_str);

/**
 * @brief Create a new graph
 * @return The newly created empty dataflow graph
 **/
plumberapi_graph_t* plumberapi_graph_new(void);

/**
 * @brief Dispose a dataflow graph
 * @param graph The graph to dispose
 * @return status code
 **/
int plumberapi_graph_free(plumberapi_graph_t* graph);

/**
 * @brief Create a new node in the dataflow graph
 * @param graph The graph where we want to add the node
 * @param node_init_str The node initialization string
 * @return The node or the error code
 **/
plumberapi_node_t plumberapi_graph_add_node(plumberapi_graph_t* graph, char const* node_init_str);

/**
 * @brief Connect two node.
 * @param graph The dataflow graph contains the two nodes we want to connect
 * @param from_node The upstream node
 * @param from_port The upstream port
 * @param to_node   The downstream node
 * @param to_port   The dowenstream port
 * @return status code
 **/
int plumberapi_graph_connect(plumberapi_graph_t* graph,
                             plumberapi_node_t from_node, char const* from_port,
                             plumberapi_node_t to_node,   char const* to_port);


/**
 * @brief Get the type name of the given port of the given node in the graph
 * @param graph The graph where the target nodes is added
 * @param node  The target node
 * @param port  The port name
 * @return The type string or NULL on error
 **/
const char* plumberapi_graph_port_type(plumberapi_graph_t* graph, plumberapi_node_t node, char const* port);

/**
 * @brief Get the list of ports that defined by the given node
 * @note This function will allocate the both the array and strings, the caller should call plumberapi_portlist_free after use
 * @param graph The graph where the node is added
 * @param node The id of the node
 * @return The result list or NULL on error
 **/
char** plumberapi_graph_node_ports(plumberapi_graph_t* graph, plumberapi_node_t node);

/**
 * @brief Dispose a used port list result
 * @param list the list to dispose
 * @return status code
 **/
int plumberapi_portlist_free(char** list);

/**
 * @brief Set the event source of the entire dataflow graph
 * @param graph The dataflow graph
 * @param node The node id
 * @param port The port
 * @return status code
 **/
int plumberapi_graph_source(plumberapi_graph_t* graph, plumberapi_node_t node, char const* port);

/**
 * @brief brief Set the sink of the dataflow graph
 * @param graph The dataflow graph
 * @param node The node id
 * @param port The port
 * @return the status code
 **/
int plumberapi_graph_sink(plumberapi_graph_t* graph, plumberapi_node_t node, char const* port);

/**
 * @brief Run the dataflow graph
 * @param graph The graph to start
 * @return status code
 **/
int plumberapi_graph_run(plumberapi_graph_t* graph);

/**
 * @brief Get the numeric configuration
 * @param key the variable to get
 * @param val The value buffer
 * @return The number of value has been read, error code when error occurs
 **/
int plumberapi_get_num_config(const char* key, int64_t* val);

/**
 * @brief Set the numeric configuration
 * @param key The key to set
 * @param val The value
 * @param The number of value accepted by Plumber, error code when error occurs
 **/
int plumberapi_set_num_config(const char* key, int64_t val);

/**
 * @brief Get a string configuration
 * @param key The key to the config
 * @param val The value buffer
 * @note The result stirng is a new copy and malloced, so the caller should dispose it after use
 * @return The number of values has been read, error code wehen error occurs
 **/
int plumberapi_get_str_config(const char* key, char** val);

/**
 * @brief Set a string configuration
 * @param key The key to config
 * @param val The value to set
 * @return The number of values accepted by Plumber, error code when error occurs
 **/
int plumberapi_set_str_config(const char* key, const char* val);

/**
 * @brief The callback function that used to construct a new dataflow graph for hot-spawn
 * @param data The additonal data
 * @return status code
 **/
typedef plumberapi_graph_t* plumberapi_graph_constructor_func_t(void* data);

/**
 * @brief Swap the binary namespace, this should be the first step to hot-spawn a new graph
 * @param cons The constructor
 * @param data The data
 * @note This function will update currently running graph without interrupting the graph (as known as non-stop deployment or hot-spawn)
 * @return status code
 **/
int plumberapi_update_graph(plumberapi_graph_constructor_func_t cons, void* data);

#endif /* __PLUMBERAPI_H__ */
