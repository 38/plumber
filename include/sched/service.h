/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The graph that represents a service
 * @file sched/service.h
 **/
#ifndef __PLUMBER_SCHED_SERVICE_H__
#define __PLUMBER_SCHED_SERVICE_H__

/**
 * @brief the previous definition of the cnode info array
 **/
typedef struct _sched_cnode_info_t sched_cnode_info_t;

/**
 * @brief the type for a node ID
 **/
typedef uint32_t sched_service_node_id_t;

/**
 * @brief define a service
 **/
typedef struct _sched_service_t sched_service_t;

/**
 * @brief a temporary buffer that is used to build a service description
 **/
typedef struct _sched_service_buffer_t sched_service_buffer_t;

/**
 * @brief descriptor a pipe in the node
 **/
typedef struct {
	sched_service_node_id_t source_node_id;      /*!< the node id for the input end*/
	sched_service_node_id_t destination_node_id; /*!< the node id for the output end*/
	runtime_api_pipe_id_t source_pipe_desc;      /*!< the pipe id for the input end*/
	runtime_api_pipe_id_t destination_pipe_desc; /*!< the pipe descriptor for the output end*/
} sched_service_pipe_descriptor_t;

/**
 * @brief convert a service to a pipe descriptor, which means treat the entire service as a pipe
 *        which is a input node, input pipe end; a output node, a output pipe end
 * @param service the target service
 * @return the converted pipe
 **/
static inline const sched_service_pipe_descriptor_t* sched_service_to_pipe_desc(const sched_service_t* service)
{
	return (const sched_service_pipe_descriptor_t*) service;
}

/**
 * @brief create a new service buffer
 * @return the newly created buffer or NULL if error happens
 **/
sched_service_buffer_t* sched_service_buffer_new(void);

/**
 * @brief make the service buffer allows us reuse the servlet instane
 * @note THIS FUNCTION SHOULD USE FOR TESTING PURPOSE ONLY <br/>
 *       Beause the type system need enforce the servlet context not being reused.
 *       So if we passed this in the normal code, the behavior is undefined
 * @param buffer the target service buffer
 * @return status code
 **/
int sched_service_buffer_allow_reuse_servlet(sched_service_buffer_t* buffer);

/**
 * @brief add a new node to the service buffer
 * @param buffer the target service buffer
 * @param sid the servlet ID aquired from runtime_stab_get_by_name
 * @return the node ID or negative error code
 **/
sched_service_node_id_t sched_service_buffer_add_node(sched_service_buffer_t* buffer, runtime_stab_entry_t sid);

/**
 * @brief add a pipe between two nodes
 * @param buffer the target service buffer
 * @param desc the pipe
 * @return the status code
 **/
int sched_service_buffer_add_pipe(sched_service_buffer_t* buffer, sched_service_pipe_descriptor_t desc);

/**
 * @brief dispose the used buffer
 * @param buffer the target buffer
 * @return status code
 **/
int sched_service_buffer_free(sched_service_buffer_t* buffer);

/**
 * @brief Construct a service from the given file descriptor
 * @note This function is used when we are deploying a new version of service. And the daemon
 *       can read the service definition from the control socket fd
 * @param fd The fd to read
 * @return The newly created service or error code
 **/
sched_service_t* sched_service_from_fd(int fd);

/**
 * @brief Dump the service to the given fd
 * @note This function will produce the data format that sched_service_from_fd can understand
 * @param service The service to dump
 * @param fd The target fd
 * @return status code
 **/
int sched_service_dump_fd(const sched_service_t* service, int fd);

/**
 * @brief construct a service def from the buffer
 * @param buffer the scheduler buffer used to create the service def
 * @return the newly created service def, NULL if error happens
 * @note this function do not dispose the buffer, the caller should clean up the buffer
 **/
sched_service_t* sched_service_from_buffer(const sched_service_buffer_t* buffer);

/**
 * @brief dispose a service def
 * @param service the service def
 * @return status code
 **/
int sched_service_free(sched_service_t* service);

/**
 * @brief create a new exec task for the node id
 * @param service the target service
 * @param nid the node id to get
 * @return the newly created task
 **/
runtime_task_t* sched_service_create_task(const sched_service_t* service, sched_service_node_id_t nid);

/**
 * @brief get pipe descriptor list of all incoming pipes
 * @param service the target service
 * @param nid the node ID
 * @param nresult the buffer used to return how many pipe is related to this node
 * @return the pointer to the head of the list, NULL if error happens
 **/
const sched_service_pipe_descriptor_t* sched_service_get_incoming_pipes(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* nresult);

/**
 * @brief get the pipe descriptor list of all outgoing pipes
 * @param service the target service
 * @param nid the node id
 * @param nresult the buffer that used to return how many pipe is returned
 * @return the pointer to the head of the list, NULL if error happens
 **/
const sched_service_pipe_descriptor_t* sched_service_get_outgoing_pipes(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* nresult);

/**
 * @brief set the input pipe of this service buffer
 * @param buffer the target service buffer
 * @param node the input node id
 * @param pipe the input pipe id
 * @return status code
 * @note a input node must have exactly one input, which is going to recieving incoming HTTP request
 **/
int sched_service_buffer_set_input(sched_service_buffer_t* buffer, sched_service_node_id_t node, runtime_api_pipe_id_t pipe);

/**
 * @brief set the output pipe of this service buffer
 * @param buffer the target service buffer
 * @param node the output node id
 * @param pipe the output pipe id
 * @return status code
 **/
int sched_service_buffer_set_output(sched_service_buffer_t* buffer, sched_service_node_id_t node, runtime_api_pipe_id_t pipe);

/**
 * @brief get the initialization arguments of a node
 * @param service the target service
 * @param nid the node ID
 * @param argc the buffer used to return the count of arguments
 * @return the pointer to the argument list
 **/
char const* const* sched_service_get_node_args(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* argc);

/**
 * @brief get the pipe flags for a given pipe of a given node
 * @param service the target service
 * @param nid the target node id
 * @param pid the target pipe id
 * @return the pipe flags, or the error code
 **/
runtime_api_pipe_flags_t sched_service_get_pipe_flags(const sched_service_t* service, sched_service_node_id_t nid, runtime_api_pipe_id_t pid);

/**
 * @brief get the number of nodes defined in this service
 * @param service the target service
 * @return the number of nodes, or error code
 **/
size_t sched_service_get_num_node(const sched_service_t* service);

/**
 * @brief get the input node of the service graph
 * @param service the target service
 * @return the node id of the input node, or error code
 **/
sched_service_node_id_t sched_service_get_input_node(const sched_service_t* service);

/**
 * @brief get the output node of the service graph
 * @param service the target service
 * @return the node id of the input node or error code
 **/
sched_service_node_id_t sched_service_get_output_node(const sched_service_t* service);

/**
 * @brief get the critical node info
 * @param service the service graph
 * @return the cnode info or NULL on error
 **/
const sched_cnode_info_t* sched_service_get_cnode_info(const sched_service_t* service);

/**
 * @brief get the profiler for this service
 * @param service the target service
 * @param node the node to start profiler
 * @return status code
 **/
int sched_service_profiler_timer_start(const sched_service_t* service, sched_service_node_id_t node);

/**
 * @brief stop the profiler timer
 * @param service the target service
 * @return status code
 **/
int sched_service_profiler_timer_stop(const sched_service_t* service);

/**
 * @brief flush the profiler result
 * @param service the service to flush
 * @return status code
 **/
int sched_service_profiler_flush(const sched_service_t* service);

/**
 * @brief get the concrete type name of the given pipe in the given node
 * @note the returned type must be the concrete type
 * @param service the service object
 * @param node the id of the node
 * @param pid the pipe id
 * @param result the result buffer
 * @return status code
 **/
int sched_service_get_pipe_type(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid, char const* * result);

/**
 * @brief get the size of the typed pipe header of the given pipe in the given node
 * @param service the service
 * @param node the id of the node
 * @param pid the pipe id
 * @return the size of the header or error code
 **/
size_t sched_service_get_pipe_type_size(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid);

/**
 * @brief set the concrete type name of the given pipe in the given node
 * @note the type name will be considered be concrete
 * @param service the target service
 * @param node the id of the node
 * @param pid the pipe id
 * @param type_name the type name we want to put in the service data
 * @param header_size the size of the header
 * @return status code
 **/
int sched_service_set_pipe_type(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid, const char* type_name, size_t header_size);

/**
 * @brief get the type expression which describes the type of pipe specified by pair <node, pid>
 * @note  the function sched_service_get_pipe_type/sched_service_set_pipe_type are the functions to access
 *        the actual concrete type for each node. But this function provides the access to the servlets
 *        PDT for the abstract type expression in the servlet
 * @param service the target service
 * @param node the node id
 * @param pid the pipe id
 * @return status code
 **/
const char* sched_service_get_pipe_type_expr(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid);
#endif /* __PLUMBER_SCHED_SERVICE_H__ */
