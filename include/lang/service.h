/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The builtin functions used by PSS languange
 * @file lang/builtin.h
 **/

#ifndef __LANG_SERVICE_H__
#define __LANG_SERVICE_H__

#define LANG_SERVICE_TYPE_MAGIC 0x31fe25d4u

/**
 * @brief Represent a service object in PSS VM
 **/
typedef struct _lang_service_t lang_service_t;

/**
 * @brief Create a new service object
 * @return status code
 **/
lang_service_t* lang_service_new();

/**
 * @brief Dispose a used service
 * @return status code
 **/
int lang_service_free();

/**
 * @brief Create a new service node in the exotic object
 * @param service The exotic service object
 * @param name The name of the node
 * @param init_args The initialization arguments
 * @return The node id
 **/
int64_t lang_service_add_node(lang_service_t* service, const char* init_args);

/**
 * @brief Get the name of the port for the give node
 * @param service The service to add
 * @param nid The node id
 * @param name The port name
 * @return The port ID
 **/
int64_t lang_service_get_port(lang_service_t* service, int64_t nid, const char* name);

/**
 * @brief Get the list of the port names
 * @param service The service 
 * @param nid The node id
 * @param input 1 for input pipes, 0 for output
 * @return The array of strings for all the port, the caller should free the memory
 **/
char** lang_service_node_port_names(const lang_service_t* service, int64_t nid, int input);

/**
 * @brief Create a pipe connects two nodes
 * @param service The port to connect
 * @param src_nid The source node id
 * @param src_pid THe source port id
 * @param dst_nid THe destination node id
 * @param dst_pid The destination port id
 * @return status code
 **/
int lang_service_add_edge(lang_service_t* service, int64_t src_nid, int64_t src_pid, int64_t dst_nid, int64_t dst_pid);

/**
 * @brief Start the service
 * @param service The service to start
 * @return status code
 **/
int lang_service_start(lang_service_t* service);
 
#endif
