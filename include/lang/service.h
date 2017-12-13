/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The PSS binding for service objects
 * @file lang/service.h
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
lang_service_t* lang_service_new(void);

/**
 * @brief Dispose a used service
 * @return status code
 **/
int lang_service_free(lang_service_t* service);

/**
 * @brief Create a new service node in the exotic object
 * @param service The exotic service object
 * @param init_args The initialization arguments
 * @return The node id
 **/
int64_t lang_service_add_node(lang_service_t* service, const char* init_args);

/**
 * @brief Get the list of the port names
 * @param service The service
 * @param nid The node id
 * @return The array of strings for all the port, the caller should free the memory
 **/
char** lang_service_node_port_names(const lang_service_t* service, int64_t nid);

/**
 * @brief Create a pipe connects two nodes
 * @param service The port to connect
 * @param src_nid The source node id
 * @param src_port THe source port name
 * @param dst_nid THe destination node id
 * @param dst_port The destination port name
 * @return status code
 **/
int lang_service_add_edge(lang_service_t* service, int64_t src_nid, const char* src_port, int64_t dst_nid, const char* dst_port);

/**
 * @brief Set the input node of the service
 * @param service The service
 * @param nid The node id
 * @param port the port name
 * @return status code
 **/
int lang_service_set_input(lang_service_t* service, int64_t nid, const char* port);

/**
 * @brief Set the output node of the service
 * @param service The service
 * @param nid The node id
 * @param port The port name
 * @return status code
 **/
int lang_service_set_output(lang_service_t* service, int64_t nid, const char* port);

/**
 * @brief Get the type expression of the given service
 * @param service The service object
 * @param nid The node id
 * @param port The port name
 * @return The type string
 **/
const char* lang_service_get_type(lang_service_t* service, int64_t nid, const char* port);

/**
 * @brief Start the service
 * @param service The service to startA
 * @param fork_twice If we want to fork twice and keep current process
 * @return status code
 **/
int lang_service_start(lang_service_t* service, int fork_twice);

/**
 * @brief Reload the service
 * @param daemon The daemon name
 * @param service The service
 * @return status code
 **/
int lang_service_reload(const char* daemon, lang_service_t* service);

#endif
