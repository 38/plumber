/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include <error.h>
#include <itc/module_types.h>
#include <itc/module.h>
#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <sched/service.h>
#include <sched/cnode.h>
#include <sched/prof.h>
#include <sched/type.h>

#include <utils/vector.h>
#include <utils/log.h>
#include <utils/string.h>

/**
 * @brief the actual defination of the service scheduer buffer
 **/
struct _sched_service_buffer_t {
	uint32_t  reuse_servlet:1;            /*!< Indicates if we allows the servlet be used twice in the graph, make sure you use this only for testing */
	vector_t* nodes;                      /*!< the list of the nodes in ADG */
	vector_t* pipes;                      /*!< all the pipes that has been added, used for the duplication check */
	sched_service_node_id_t input_node;   /*!< the entry point of this service */
	runtime_api_pipe_id_t input_pipe;     /*!< the entry pipe of this service */
	sched_service_node_id_t output_node;  /*!< the exit point of this service */
	runtime_api_pipe_id_t output_pipe;    /*!< the output pipe of this service */
};

/**
 * @brief the data structure that used to represent a node in the service
 **/
typedef struct {
	uint16_t incoming_count;                    /*!< how many incoming pipes */
	uint16_t outgoing_count;                    /*!< how many outgoing pipes */
	runtime_stab_entry_t servlet_id;            /*!< the servlet ID */
	const void* args;                           /*!< the argument for this node */
	char**   pipe_type;                         /*!< the concrete type of the pipe */
	size_t*  pipe_header_size;                  /*!< the size of pipe header */
	runtime_task_flags_t flags;                 /*!< the additional task flags */
	sched_service_pipe_descriptor_t* outgoing;  /*!< outgoing list */
	uintpad_t __padding__[0];
	sched_service_pipe_descriptor_t incoming[0];/*!< the incoming list */
} _node_t;
STATIC_ASSERTION_SIZE(_node_t, incoming, 0);
STATIC_ASSERTION_LAST(_node_t, incoming);
/* We should make sure that the pipe counter won't overflow */
STATIC_ASSERTION_SIZE(_node_t, incoming_count, sizeof(runtime_api_pipe_id_t));
STATIC_ASSERTION_SIZE(_node_t, outgoing_count, sizeof(runtime_api_pipe_id_t));

/**
 * @brief the defination for the service
 * @note The type of nodes array is not a mistake. Since _node_t is variant length data structure
 *       We can not allocate all the memory needed by nodes in the begining. That is why we use _node_t*
 *       rather than _node_t for the node array
 **/
struct _sched_service_t {
	sched_service_node_id_t input_node;   /*!< the entry point of this service */
	sched_service_node_id_t output_node;  /*!< the exit point of this service */
	runtime_api_pipe_id_t input_pipe;     /*!< the entry pipe of this service */
	runtime_api_pipe_id_t output_pipe;    /*!< the output pipe of this service */
	sched_cnode_info_t*   c_nodes;        /*!< the critical node */
	size_t node_count;                    /*!< how many nodes in this service */
	sched_prof_t*         profiler;       /*!< the profiler for this service */
	uintpad_t __padding__[0];
	_node_t*  nodes[0];                   /*!< the node list */
};

STATIC_ASSERTION_SIZE(sched_service_t, nodes, 0);
STATIC_ASSERTION_LAST(sched_service_t, nodes);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, input_node, sched_service_pipe_descriptor_t, source_node_id);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, output_node, sched_service_pipe_descriptor_t, destination_node_id);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, input_pipe, sched_service_pipe_descriptor_t, source_pipe_desc);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, output_pipe, sched_service_pipe_descriptor_t, destination_pipe_desc);

/**
 * @brief define a node of the service
 **/
typedef struct {
	runtime_stab_entry_t servlet_id;   /*!< the servlet reference ID */
	runtime_task_flags_t        flags;        /*!< the flags of the task */
} _sched_service_buffer_node_t;

/**
 * @brief allocate a new service
 * @param num_nodes number of nodes in the service
 * @return the newly created service, NULL when error
 **/
static inline sched_service_t* _create_service(size_t num_nodes)
{
	size_t size = num_nodes * sizeof(_node_t*) + sizeof(sched_service_t);

	sched_service_t* ret = (sched_service_t*)malloc(size);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for service def");
	ret->node_count = num_nodes;
	memset(ret->nodes, 0, size - sizeof(sched_service_t));
	ret->c_nodes = NULL;
	return ret;
}

/**
 * @brief create a new node for service
 * @param servlet the servlet ID
 * @param flags the task flags
 * @param incoming_count how many incoming pipes
 * @param outgoing_count how many outgoing pipes
 * @param reuse_servlet if we allows resuse the servlet instance, only used for testing
 * @return the newly created node or NULL when error
 **/
static inline _node_t* _create_node(runtime_stab_entry_t servlet, runtime_task_flags_t flags, uint32_t incoming_count, uint32_t outgoing_count, int reuse_servlet)
{
	size_t size = (incoming_count + outgoing_count) * sizeof(sched_service_pipe_descriptor_t) + sizeof(_node_t);
	_node_t* ret = (_node_t*)malloc(size);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for service node");

	ret->outgoing = ret->incoming + incoming_count;
	ret->incoming_count = ret->outgoing_count = 0;
	ret->servlet_id = servlet;
	ret->flags = flags;

	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
		ERROR_LOG_GOTO(ERR, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t npipes = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == npipes)
		ERROR_LOG_GOTO(ERR, "Cannot get the size of the PDT");

	if(NULL == (ret->pipe_type = (char**)calloc(npipes, sizeof(char*))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the type buffer");

	if(NULL == (ret->pipe_header_size = (size_t*)calloc(npipes, sizeof(size_t))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the type size buffer");

	if(ERROR_CODE(int) == runtime_stab_set_owner(servlet, ret, reuse_servlet))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the owner of the servlet");

	return ret;

ERR:
	if(NULL != ret)
	{
		if(NULL != ret->pipe_type) free(ret->pipe_type);
		if(NULL != ret->pipe_header_size) free(ret->pipe_header_size);
		free(ret);
	}
	return NULL;
}

/**
 * @brief dispose a used service node
 * @param node the node to dispose
 * @return status code
 **/
static inline int _dispose_node(_node_t* node)
{
	if(NULL != node->pipe_type)
	{

		size_t count = 0;
		runtime_stab_entry_t sid = node->servlet_id;
		const runtime_pdt_t* pdt = runtime_stab_get_pdt(sid);
		if(NULL == pdt)
			LOG_WARNING("Cannot get the PTD for servlet %u", sid);

		if(ERROR_CODE(size_t) == (count = runtime_pdt_get_size(pdt)))
		{
			LOG_WARNING("Cannot get the size of the PDT table");
			count = 0;
		}

		size_t i;
		for(i = 0; i < count; i ++)
			if(NULL != node->pipe_type[i])
				free(node->pipe_type[i]);
		free(node->pipe_type);
	}

	if(NULL != node->pipe_header_size)
		free(node->pipe_header_size);

	free(node);

	return 0;
}

/**
 * @brief validate the node ID
 * @param buffer the service buffer
 * @param node_id the node ID in the service buffer
 * @return status code
 **/
static inline int _check_node_id(const sched_service_buffer_t* buffer, sched_service_node_id_t node_id)
{
	if(node_id == ERROR_CODE(sched_service_node_id_t)) ERROR_RETURN_LOG(int, "Invalid node id %u", node_id);
	if(node_id >= vector_length(buffer->nodes)) ERROR_RETURN_LOG(int, "Node #%u doesn't exist", node_id);
	return 0;
}

/**
 * @brief validate the pipe
 * @param buffer the service buffer
 * @param node_id the node ID
 * @param pipe_id the pipe ID in that node
 * @param incoming if it's not 0, means should be a incoming pipe, otherwise it should be a outgoing pipe
 * @return status code
 **/
static inline int _check_pipe(const sched_service_buffer_t* buffer, sched_service_node_id_t node_id, runtime_api_pipe_id_t pipe_id, int incoming)
{
	if(_check_node_id(buffer, node_id) == ERROR_CODE(int)) return ERROR_CODE(int);
	const _sched_service_buffer_node_t* node = VECTOR_GET_CONST(_sched_service_buffer_node_t, buffer->nodes, node_id);
	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot read the buffer nodes table");

	size_t npipes = runtime_stab_num_pipes(node->servlet_id);

	if(npipes == ERROR_CODE(size_t) || npipes <= pipe_id || pipe_id == ERROR_CODE(runtime_api_pipe_id_t))
		ERROR_RETURN_LOG(int, "Invalid pipe ID %d", pipe_id);

	runtime_api_pipe_flags_t flags = runtime_stab_get_pipe_flags(node->servlet_id, pipe_id);

	if(incoming)
	{
		if(!RUNTIME_API_PIPE_IS_INPUT(flags)) ERROR_RETURN_LOG(int, "Input side of the pipe expected");
	}
	else if(!RUNTIME_API_PIPE_IS_OUTPUT(flags)) ERROR_RETURN_LOG(int, "Output side of the pipe expected");

	return 0;
}

/**
 * @brief check if the service is a well formed service, which means input and output pipe
 *        are defined and service do not contains a loop
 * @param service the service to check
 * @return the status code
 **/
static inline int _check_service_graph(const sched_service_t* service)
{
	if(service->nodes[service->input_node]->incoming_count != 0)
		ERROR_RETURN_LOG(int, "Input node cannot depens on other node");

	if(runtime_stab_get_num_input_pipe(service->nodes[service->input_node]->servlet_id) != 1)
		ERROR_RETURN_LOG(int, "Input node must have exactly 1 input");

	/* Connecting the output node to another node doesn't make sense, because it won't affect the output */
	if(service->nodes[service->output_node]->outgoing_count != 0)
		ERROR_RETURN_LOG(int, "Cannot connect output node to another node");

	sched_service_node_id_t id;
	uint32_t i;
	size_t  nz = service->node_count, next;
	//int deg[service->node_count];
	int* deg = (int*)malloc(sizeof(int) *service->node_count);
	if(NULL == deg) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the degree array");

	for(id = 0; (size_t)id < service->node_count; id ++)
	{
		deg[id] = service->nodes[id]->incoming_count;
		if(deg[id] == 0 && id != service->input_node)
		{
			LOG_WARNING("node #%d has no incoming pipe", id);
			nz --;
		}
	}

	for(next = service->input_node; nz > 0 && next < service->node_count; nz--)
	{
		for(i = 0; i < service->nodes[next]->outgoing_count; i ++)
			deg[service->nodes[next]->outgoing[i].destination_node_id] --;
		deg[next] = -1;
		for(next = 0; next < service->node_count && deg[next] != 0; next ++);
	}

	free(deg);

	if(nz != 0) ERROR_RETURN_LOG(int, "A circular dependency is detected in the service graph");

	return 0;
}

sched_service_buffer_t* sched_service_buffer_new()
{
	sched_service_buffer_t* ret = (sched_service_buffer_t*)malloc(sizeof(sched_service_buffer_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the service def buffer");

	if(NULL == (ret->nodes = vector_new(sizeof(_sched_service_buffer_node_t), SCHED_SERVICE_BUFFER_NODE_LIST_INIT_SIZE)))
		ERROR_LOG_GOTO(ERR, "Cannot create vector for the service buffer node list");

	if(NULL == (ret->pipes = vector_new(sizeof(sched_service_pipe_descriptor_t), SCHED_SERVICE_BUFFER_OUT_GOING_LIST_INIT_SIZE)))
		ERROR_LOG_GOTO(ERR, "Cannot create vector for the service buffer pipe descriptor list");

	/* fill the undetermined field with error code, so that we can enforce this later */
	ret->input_node = ERROR_CODE(sched_service_node_id_t);
	ret->output_node = ERROR_CODE(sched_service_node_id_t);
	ret->input_pipe = ERROR_CODE(runtime_api_pipe_id_t);
	ret->output_pipe = ERROR_CODE(runtime_api_pipe_id_t);
	ret->reuse_servlet = 0;

	return ret;
ERR:
	if(ret != NULL)
	{
		if(ret->pipes != NULL) vector_free(ret->pipes);
		free(ret);
	}
	return NULL;
}

int sched_service_buffer_free(sched_service_buffer_t* buffer)
{
	int rc = 0;
	if(NULL == buffer) return 0;

	if(buffer->nodes != NULL)
	{
		if(vector_free(buffer->nodes) < 0)
		{
			LOG_WARNING("cannot dispose the nodes vector for the service buffer");
			rc = ERROR_CODE(int);
		}
	}

	if(buffer->pipes != NULL && vector_free(buffer->pipes) < 0)
	{
		LOG_WARNING("cannot dispose the pipes vector for the service buffer");
		rc = ERROR_CODE(int);
	}

	free(buffer);

	return rc;
}

int sched_service_buffer_allow_reuse_servlet(sched_service_buffer_t* buffer)
{
	if(NULL == buffer)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	buffer->reuse_servlet = 1;

	return 0;
}

sched_service_node_id_t sched_service_buffer_add_node(sched_service_buffer_t* buffer, runtime_stab_entry_t sid)
{
	if(NULL == buffer || sid == ERROR_CODE(runtime_stab_entry_t))
		ERROR_RETURN_LOG(sched_service_node_id_t, "Invalid arguments");

	sched_service_node_id_t ret = (sched_service_node_id_t)vector_length(buffer->nodes);

	_sched_service_buffer_node_t node = {
		.servlet_id = sid,
	};

	vector_t* result;
	if((result = vector_append(buffer->nodes, &node)) == NULL)
		ERROR_RETURN_LOG(sched_service_node_id_t, "Cannot insert new node to the node list");
	buffer->nodes = result;

#ifdef LOG_TRACE_ENABLED
	char name_buffer[128];
	uint32_t argc, i;
	char const * const * argv;
	string_buffer_t sbuf;

	string_buffer_open(name_buffer, sizeof(name_buffer), &sbuf);
	if(NULL == (argv = runtime_stab_get_init_arg(sid, &argc)))
		string_buffer_appendf(&sbuf, "<INVALID_SID_%d>", sid);
	else
	{
		for(i = 0; i < argc; i ++)
			if(i == 0)
				string_buffer_append(argv[i], &sbuf);
		    else
			    string_buffer_appendf(&sbuf, " %s", argv[i]);
	}
	string_buffer_close(&sbuf);

	LOG_TRACE("Servlet (SID = %d) `%s' has been added as node #%u", sid, name_buffer, ret);
#endif
	return ret;
}

int sched_service_buffer_add_pipe(sched_service_buffer_t* buffer, sched_service_pipe_descriptor_t desc)
{
	if(NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");

	sched_service_node_id_t src_node = desc.source_node_id;
	runtime_api_pipe_id_t src_pipe = desc.source_pipe_desc;
	sched_service_node_id_t dst_node = desc.destination_node_id;
	runtime_api_pipe_id_t dst_pipe = desc.destination_pipe_desc;

	if(_check_pipe(buffer, src_node, src_pipe, 0) == ERROR_CODE(int))
		ERROR_RETURN_LOG(int, "Invalid output pipe <NID=%u, PID=%u>", src_node, src_pipe);

	if(_check_pipe(buffer, dst_node, dst_pipe, 1) == ERROR_CODE(int))
		ERROR_RETURN_LOG(int, "Invalid input pipe <NID=%u, PID=%u>", dst_node, dst_pipe);

	/* Check if the pipe slot has been used
	 * Because this function is only used when the service defination is updated, so there's almost no
	 * performance impact. The duplication check is just a linear search */

	uint32_t i;
	for(i = 0; i < vector_length(buffer->pipes); i ++)
	{
		const sched_service_pipe_descriptor_t* desc_ptr = VECTOR_GET_CONST(sched_service_pipe_descriptor_t, buffer->pipes, i);

		if((src_node == desc_ptr->source_node_id && src_pipe == desc_ptr->source_pipe_desc) ||
		   (src_node == desc_ptr->destination_node_id && src_pipe == desc_ptr->destination_pipe_desc))
			ERROR_RETURN_LOG(int, "Pipe slot <NID=%u,PID=%u> has already in use", src_node, src_pipe);

		if((dst_node == desc_ptr->source_node_id && dst_pipe == desc_ptr->source_pipe_desc) ||
		   (dst_node == desc_ptr->destination_node_id && dst_pipe == desc_ptr->destination_pipe_desc))
			ERROR_RETURN_LOG(int, "Pipe slot <NID=%u,PID=%u> has already in use", dst_node, dst_pipe);
	}

	_sched_service_buffer_node_t* node = VECTOR_GET(_sched_service_buffer_node_t, buffer->nodes, src_node);

	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot read the node list in a service buffer");

	vector_t* result = vector_append(buffer->pipes, &desc);
	if(NULL == result) ERROR_RETURN_LOG(int, "Cannot insert the new descriptor to the pipe list");
	buffer->pipes = result;

	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot read the node list");

	LOG_TRACE("Defined new pipe <NID=%d, SID=%d> -> <NID=%d, SID=%d>", src_node, src_pipe, dst_node, dst_pipe);
	return 0;
}

int sched_service_buffer_set_input(sched_service_buffer_t* buffer, sched_service_node_id_t node, runtime_api_pipe_id_t pipe)
{
	if(NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(_check_pipe(buffer, node, pipe, 1) < 0) return ERROR_CODE(int);

	buffer->input_node = node;
	buffer->input_pipe = pipe;
	return 0;
}

int sched_service_buffer_set_output(sched_service_buffer_t* buffer, sched_service_node_id_t node, runtime_api_pipe_id_t pipe)
{
	if(NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(_check_pipe(buffer, node, pipe, 0) < 0) return ERROR_CODE(int);

	buffer->output_node = node;
	buffer->output_pipe = pipe;
	return 0;
}

/**
 * @brief the compare function used to sort the pipe descriptor list by it's source pipe id
 * @param l the left operator
 * @param r the right operator
 * @return compare result
 **/
static inline int _compare_src_pipe(const void* l, const void* r)
{
	const sched_service_pipe_descriptor_t* left = (const sched_service_pipe_descriptor_t*)l;
	const sched_service_pipe_descriptor_t* right = (const sched_service_pipe_descriptor_t*)r;

	if(left->source_pipe_desc < right->source_pipe_desc) return -1;
	if(left->source_pipe_desc > right->source_pipe_desc) return 1;
	return 0;
}

sched_service_t* sched_service_from_buffer(const sched_service_buffer_t* buffer)
{
	uint32_t i;
	uint32_t* incoming_count = NULL;
	uint32_t* outgoing_count = NULL;
	if(NULL == buffer) ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t num_nodes = vector_length(buffer->nodes);

	if(_check_pipe(buffer, buffer->input_node, buffer->input_pipe, 1) < 0 || _check_pipe(buffer, buffer->output_node, buffer->output_pipe, 0) < 0)
		ERROR_PTR_RETURN_LOG("Invalid input or output of the service");

	sched_service_t* ret = _create_service(num_nodes);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create a new service def");

	size_t counting_array_size = sizeof(uint32_t) * num_nodes;

	if(num_nodes > SCHED_SERVICE_MAX_NUM_NODES)
		ERROR_PTR_RETURN_LOG("Too many nodes in the service graph. (Current value: %zu, Limit: %lu)", num_nodes, SCHED_SERVICE_MAX_NUM_NODES);

	if(vector_length(buffer->pipes) > SCHED_SERVICE_MAX_NUM_EDGES)
		ERROR_PTR_RETURN_LOG("Too many edges in the service graph. (Current value: %zu, Limit: %lu)", vector_length(buffer->pipes), SCHED_SERVICE_MAX_NUM_EDGES);

	if(NULL == (incoming_count = (uint32_t*)calloc(1, counting_array_size))) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate incoming counting array");

	if(NULL == (outgoing_count = (uint32_t*)calloc(1, counting_array_size))) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate outgoing counting array");


	for(i = 0; i < vector_length(buffer->pipes); i ++)
	{
		const sched_service_pipe_descriptor_t* pipe = VECTOR_GET_CONST(sched_service_pipe_descriptor_t, buffer->pipes, i);
		if(NULL == pipe)
		{
			LOG_WARNING("Could not read the pipe list in the service buffer");
			continue;
		}

		sched_service_node_id_t src_node = pipe->source_node_id;
		sched_service_node_id_t dst_node = pipe->destination_node_id;

		incoming_count[dst_node] ++;
		outgoing_count[src_node] ++;
	}

	for(i = 0; i < num_nodes; i ++)
	{
		const _sched_service_buffer_node_t* node = VECTOR_GET_CONST(_sched_service_buffer_node_t, buffer->nodes, i);
		if(NULL == node) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the node table in the service buffer");
		if(NULL == (ret->nodes[i] = _create_node(node->servlet_id, node->flags, incoming_count[i], outgoing_count[i], (buffer->reuse_servlet != 0))))
			ERROR_LOG_GOTO(ERR, "Cannot create node in the service def");
	}

	for(i = 0; i < vector_length(buffer->pipes); i ++)
	{
		const sched_service_pipe_descriptor_t* pipe = VECTOR_GET_CONST(sched_service_pipe_descriptor_t, buffer->pipes, i);
		if(NULL == pipe)
		{
			LOG_WARNING("could not read the pipe list in the service buffer");
			continue;
		}

		sched_service_node_id_t src_node = pipe->source_node_id;
		sched_service_node_id_t dst_node = pipe->destination_node_id;

		memcpy(ret->nodes[dst_node]->incoming + (ret->nodes[dst_node]->incoming_count ++), pipe, sizeof(sched_service_pipe_descriptor_t));
		memcpy(ret->nodes[src_node]->outgoing + (ret->nodes[src_node]->outgoing_count ++), pipe, sizeof(sched_service_pipe_descriptor_t));
	}

	/**
	 * The reason why we need to sort the outgoing pipes is simple.
	 * Because we have shadow pipes, and to declare the shadow pipes we need to know the target pipe id first.
	 * In here, we assume the pipe_define function assign the pipe id incrementally, so that the target pipe *must* have
	 * a smaller pipe id.
	 * In addition, we assume the scheduler initialize the pipe in the order how outgoing pipes are stored in this array.
	 * If we sort the outgoing pipe by its source id, this can guarantee all the target pipe has been initialized before
	 * the attemption of initializing its shadow.
	 * The input pipe is not actually improtant in this case, because when the scheduler initialize the outputs, that means
	 * the task is ready, and all valid input pipes are already initialized
	 **/
	for(i = 0; i < num_nodes; i ++)
		qsort(ret->nodes[i]->outgoing, ret->nodes[i]->outgoing_count, sizeof(sched_service_pipe_descriptor_t), _compare_src_pipe);

	ret->input_node = buffer->input_node;
	ret->input_pipe = buffer->input_pipe;
	ret->output_node = buffer->output_node;
	ret->output_pipe = buffer->output_pipe;

	if(_check_service_graph(ret) == ERROR_CODE(int)) ERROR_LOG_GOTO(ERR, "Invalid service graph");

	free(incoming_count);
	free(outgoing_count);

	incoming_count = outgoing_count = NULL;

	if(NULL == (ret->c_nodes = sched_cnode_analyze(ret)))
		ERROR_LOG_GOTO(ERR, "Cannot analyze the critical node");
#ifdef ENABLE_PROFILER
	if(ERROR_CODE(int) == sched_prof_new(ret, &ret->profiler))
		LOG_WARNING("Cannot initialize the profiler");
#else
	ret->profiler = NULL;
#endif

	if(ERROR_CODE(int) == sched_type_check(ret))
		ERROR_LOG_GOTO(ERR, "Service type checker failed");

	return ret;
ERR:
	if(ret != NULL)
	{
		for(i = 0; i < num_nodes; i ++)
			if(ret->nodes[i] != NULL)
				_dispose_node(ret->nodes[i]);
		if(ret->c_nodes != NULL) sched_cnode_info_free(ret->c_nodes);
		free(ret);
	}
	if(incoming_count != NULL) free(incoming_count);
	if(outgoing_count != NULL) free(outgoing_count);
	return NULL;
}

int sched_service_free(sched_service_t* service)
{
	uint32_t i;
	int rc = 0;

	if(NULL == service) return ERROR_CODE(int);

	for(i = 0; i < service->node_count; i ++)
		if(service->nodes[i] != NULL && ERROR_CODE(int) == _dispose_node(service->nodes[i]))
			rc = ERROR_CODE(int);

	if(NULL != service->c_nodes && ERROR_CODE(int) == sched_cnode_info_free(service->c_nodes))
		rc = ERROR_CODE(int);

#ifdef ENABLE_PROFILER
	if(NULL != service->profiler && ERROR_CODE(int) == sched_prof_free(service->profiler))
		rc = ERROR_CODE(int);
#endif

	free(service);
	return rc;
}


runtime_task_t* sched_service_create_task(const sched_service_t* service, sched_service_node_id_t nid)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || (uint32_t)nid >= service->node_count)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	const _node_t* node = service->nodes[nid];
	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node %u is NULL", nid);

	runtime_task_t* task = runtime_stab_create_exec_task(node->servlet_id, node->flags);
	if(NULL == task) ERROR_PTR_RETURN_LOG("Cannot create new task for service node #%u", nid);
	return task;
}

const sched_service_pipe_descriptor_t* sched_service_get_incoming_pipes(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* nresult)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count || NULL == nresult)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	const _node_t* node = service->nodes[nid];
	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node #%u is NULL", nid);
	*nresult = node->incoming_count;
	return node->incoming;
}

const sched_service_pipe_descriptor_t* sched_service_get_outgoing_pipes(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* nresult)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count || NULL == nresult)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	const _node_t* node = service->nodes[nid];

	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node #%d is NULL", nid);
	*nresult = node->outgoing_count;
	return node->outgoing;
}

char const* const* sched_service_get_node_args(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* argc)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count || NULL == argc)
		ERROR_PTR_RETURN_LOG("Invalid arguments");
	const _node_t* node = service->nodes[nid];
	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node #%d is NULL", nid);
	return runtime_stab_get_init_arg(node->servlet_id, argc);
}

runtime_api_pipe_flags_t sched_service_get_pipe_flags(const sched_service_t* service, sched_service_node_id_t nid, runtime_api_pipe_id_t pid)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count)
		ERROR_RETURN_LOG(runtime_api_pipe_flags_t, "Invalid arguments");
	const _node_t* node = service->nodes[nid];

	if(NULL == node) ERROR_RETURN_LOG(runtime_api_pipe_flags_t, "Invalid service def, node #%d is NULL", nid);
	return runtime_stab_get_pipe_flags(node->servlet_id, pid);
}

size_t sched_service_get_num_node(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(size_t, "Invalid arguments");

	return service->node_count;
}

sched_service_node_id_t sched_service_get_input_node(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(sched_service_node_id_t, "Invalid arguments");

	return service->input_node;
}

sched_service_node_id_t sched_service_get_output_node(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(sched_service_node_id_t, "Invalid arguments");

	return service->output_node;
}

const sched_cnode_info_t* sched_service_get_cnode_info(const sched_service_t* service)
{
	if(NULL == service) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return service->c_nodes;
}

int sched_service_profiler_timer_start(const sched_service_t* service, sched_service_node_id_t node)
{
	if(NULL == service || node == ERROR_CODE(sched_service_node_id_t)) ERROR_RETURN_LOG(int, "Invlaid arguments");

	if(service->profiler == NULL) return 0;

	return sched_prof_start_timer(service->profiler, node);
}

int sched_service_profiler_timer_stop(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(service->profiler == NULL) return 0;

	return sched_prof_stop_timer(service->profiler);
}

int sched_service_profiler_flush(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(service->profiler == NULL) return 0;

	return sched_prof_flush(service->profiler);
}

int sched_service_get_pipe_type(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid, char const* * result)
{
	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid ||
	   NULL == result)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_stab_entry_t servlet = service->nodes[node]->servlet_id;
	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
		ERROR_RETURN_LOG(int, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t pcount = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pcount)
		ERROR_RETURN_LOG(int, "Cannot get the size of the PDT");

	if(pcount <= pid)
		ERROR_RETURN_LOG(int, "Invalid pipe id");

	*result = service->nodes[node]->pipe_type[pid];

	return 0;
}

size_t sched_service_get_pipe_type_size(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid)
{

	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	runtime_stab_entry_t servlet = service->nodes[node]->servlet_id;
	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
		ERROR_RETURN_LOG(size_t, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t pcount = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pcount)
		ERROR_RETURN_LOG(size_t, "Cannot get the size of the PDT");

	if(pcount <= pid)
		ERROR_RETURN_LOG(size_t, "Invalid pipe id");

	return service->nodes[node]->pipe_header_size[pid];
}

int sched_service_set_pipe_type(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid, const char* type_name, size_t header_size)
{
	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid ||
	   NULL == type_name || header_size == ERROR_CODE(size_t))
		ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_stab_entry_t servlet = service->nodes[node]->servlet_id;
	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
		ERROR_RETURN_LOG(int, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t pcount = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pcount)
		ERROR_RETURN_LOG(int, "Cannot get the size of the PDT");

	if(pcount <= pid)
		ERROR_RETURN_LOG(int, "Invalid pipe id");

	if(NULL != (service->nodes[node]->pipe_type[pid]))
	{
		free(service->nodes[node]->pipe_type[pid]);
		service->nodes[node]->pipe_type[pid] = NULL;
	}

	size_t len = strlen(type_name);

	if(NULL == (service->nodes[node]->pipe_type[pid] = (char*)malloc(len + 1)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the new type name");

	memcpy(service->nodes[node]->pipe_type[pid], type_name, len + 1);
	service->nodes[node]->pipe_header_size[pid] = header_size;

	/* Finally we run the callback function attached to this PD */
	runtime_api_pipe_type_callback_t callback;
	void* data;
	if(ERROR_CODE(int) == runtime_pdt_get_type_hook(pdt, pid, &callback, &data))
		ERROR_RETURN_LOG(int, "Cannot get the callback function from PDT");

	if(NULL != callback && ERROR_CODE(int) == callback(RUNTIME_API_PIPE_FROM_ID(pid), type_name, data))
		ERROR_RETURN_LOG(int, "The callbak function returns an error, NID = %u, PID = %u, type_name = %s, data = %p", node, pid, type_name, data);

	return 0;
}

const char* sched_service_get_pipe_type_expr(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid)
{
	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid)
		ERROR_PTR_RETURN_LOG("Invlaid arguments");

	const runtime_pdt_t* pipe_table = runtime_stab_get_pdt(service->nodes[node]->servlet_id);

	if(NULL == pipe_table)
		ERROR_PTR_RETURN_LOG("Cannot get the pipe table for node %u", node);

	return runtime_pdt_type_expr(pipe_table, pid);
}


/****************** The Dump/Load Features ***********************************/

/**
 * @brief The header of the dumpped servicec
 **/
typedef struct __attribute__((packed)) {
	uint32_t node_count;   /*!< The number of nodes in the service */
	uint32_t edge_count;   /*!< The number of edges in the service */
	uint32_t input_node;   /*!< The input node id */
	uint32_t output_node;  /*!< The output node id */
} _dump_header_t;

/**
 * @brief A dumpped string
 **/
typedef struct __attribute__((packed)) {
	size_t length;   /*!< The length of the string */
	char data[0];    /*!< The actual data section of the string */
} _dump_string_t;

typedef enum {
	_READ,
	_WRITE
} _fd_io_type_t;

/**
 * @brief Perform An IO operation on given FD, because the read/write syscall actually not
 *        guareentee the full data segment has been read/write completely, thus we should have a
 *        wrapper for this
 * @param fd The file descriptor
 * @param buf The buffer
 * @param size The size of the data
 * @param type The type of the IO either read or write
 * @return status code
 **/
static inline int _fd_io(int fd, void* buf, size_t size, _fd_io_type_t type)
{
	ssize_t bytes_to_io = (ssize_t)size, rc;
	for(;bytes_to_io > 0; bytes_to_io -= rc, buf = ((uint8_t*)buf) + rc)
		if((rc = type == _READ ? read(fd, buf, (size_t)bytes_to_io) :
		                         write(fd, buf, (size_t)bytes_to_io)) <= 0)
		{
			if(rc == 0)
				ERROR_RETURN_LOG(int, type == _READ ? "Malformed data in FD" : "FD is closed");
			else if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
				rc = 0;
			else
				ERROR_RETURN_LOG_ERRNO(int, "Cannot perfome IO operation on FD %d", fd);
		}
	return 0;
}

/**
 * @brief Dump string to the FD
 * @param fd The file descriptor
 * @param string  The actual  string
 **/
static inline int _dump_string(int fd, const char* string)
{
	_dump_string_t data = {
		.length = strlen(string)
	};

	if(ERROR_CODE(int) == _fd_io(fd, &data, sizeof(data), _WRITE))
		ERROR_RETURN_LOG(int, "Cannot write the string header");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	if(ERROR_CODE(int) == _fd_io(fd, (void*)string, data.length, _WRITE))
		ERROR_RETURN_LOG(int, "Cannot write the string content");
#pragma GCC diagnostic pop

	return 0;
}

/**
 * @brief Read a string the file descriptor
 * @param fd THe file descriptor
 * @return The newly allocated memory which contains the string, caller should free it
 **/
static char* _read_string(int fd)
{
	_dump_string_t header;
	if(ERROR_CODE(int) == _fd_io(fd, &header, sizeof(header), _READ))
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot read the string header");

	char* ret = (char*)malloc(header.length + 1);
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for string");

	ret[header.length] = 0;
	if(ERROR_CODE(int) == _fd_io(fd, ret, header.length, _READ))
		ERROR_LOG_GOTO(ERR, "Cannot read the string conetnt");

	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

/**
 * @details Actual data layout:
 *           +-----------------------------------------------------------------------------------+
 *           |  Dump Header| Input Port | Output Port |  Node definitions  |   Pipe definitions  |
 *           +-----------------------------------------------------------------------------------+
 *         Node definitoin:
 *           +---------------------------------------------------------+
 *           |  Node Path String | argc(uint32_t) |  Node Arg Strings  |
 *           ----------------------------------------------------------+
 *         Pipe definition:
 *           +--------------------------------------------------------------------------+
 *           |  Source Node Id | Destination Node Id | Source Port Name | Dest Port Name|
 *           +--------------------------------------------------------------------------+
 **/
sched_service_t* sched_service_from_fd(int fd)
{
	sched_service_t* ret = NULL;
	if(fd < 0) ERROR_PTR_RETURN_LOG("Invalid arguments");

	_dump_header_t header;
	if(ERROR_CODE(int) == _fd_io(fd, &header, sizeof(header), _READ))
		ERROR_PTR_RETURN_LOG("Cannot read the service header from FD");

	uint32_t i,j, argc = 0;
	char** argv = NULL;
	sched_service_buffer_t* buffer = sched_service_buffer_new();
	char* input_port = NULL, *output_port = NULL;
	if(NULL == buffer)
		ERROR_PTR_RETURN_LOG("Cannot create service buffer to reconstruct the service");

	if(NULL == (input_port = _read_string(fd)))
		ERROR_PTR_RETURN_LOG("Cannot read the input port name from FD");
	else
		LOG_INFO("Input port: %s", input_port);

	if(NULL == (output_port = _read_string(fd)))
		ERROR_PTR_RETURN_LOG("Cannot read the output port name from FD");
	else
		LOG_INFO("Output port: %s", output_port);

	if(header.node_count > SCHED_SERVICE_MAX_NUM_NODES)
		ERROR_PTR_RETURN_LOG("Invalid graph: too many nodes in the graph");

	if(header.edge_count > SCHED_SERVICE_MAX_NUM_EDGES)
		ERROR_PTR_RETURN_LOG("Invalid graph: too many edges in the graph");

	runtime_stab_entry_t* servlet_ids = (runtime_stab_entry_t*)malloc(sizeof(runtime_stab_entry_t) * header.node_count);
	if(NULL == servlet_ids)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the servlet id array");

	/* First, load the nodes for the service */
	for(i = 0; i < header.node_count; i ++)
	{
		int error = 1;
		argc = 0;
		argv = NULL;
		char* binary = _read_string(fd);
		if(NULL == binary)
			ERROR_LOG_GOTO(NODE_ERR, "Cannot read binary path from the fd");

		if(ERROR_CODE(int) == _fd_io(fd, &argc, sizeof(argc), _READ))
			ERROR_LOG_GOTO(NODE_ERR, "Cannot read the number of arguments");

		if(NULL == (argv = (char**)calloc(sizeof(char*), argc)))
			ERROR_LOG_ERRNO_GOTO(NODE_ERR, "Cannot allocate memory for the argc array");

		for(j = 0; j < argc; j ++)
		{
			if(NULL == (argv[j] = _read_string(fd)))
				ERROR_LOG_GOTO(NODE_ERR, "Cannot read argument content from the fd");
		}

		runtime_stab_entry_t sid = servlet_ids[i] = runtime_stab_load(argc, (char const* const*)argv, binary);

#if defined(LOG_ERROR_ENABLED)
		char arg[1024];
		uint32_t k;
		string_buffer_t sbuf;
		string_buffer_open(arg, sizeof(arg), &sbuf);
		for(k = 0; k < argc; k ++)
		{
			string_buffer_append(argv[k], &sbuf);
			if(k != argc - 1)
				string_buffer_append(" ", &sbuf);
		}
		string_buffer_close(&sbuf);
#endif

		if(ERROR_CODE(runtime_stab_entry_t) == sid)
		{
			ERROR_LOG_GOTO(NODE_ERR, "Cannot load servlet [binary = %s, arg = %s]", binary, arg);
		}
		else
			LOG_INFO("Servlet [Binary = %s, Arg = %s] has been loaded as servlet %u", binary, arg, sid);

		sched_service_node_id_t nid = sched_service_buffer_add_node(buffer, sid);
		if(ERROR_CODE(sched_service_node_id_t) == nid)
			ERROR_LOG_GOTO(NODE_ERR, "Cannot add servlet [SID = %u, Arg = %s, Binary = %s]", sid, arg, binary);
		else
			LOG_INFO("Service node has been added [NID = %u, SID = %u, Arg = %s, Binary = %s]", nid, sid, arg, binary);

		if(nid != i)
			ERROR_LOG_GOTO(NODE_ERR, "Unexpected node id (Excepted: %u, Actually: %u)", sid, nid);

		error = 0;
NODE_ERR:
		for(j = 0; j < argc; j ++)
			if(argv[j] != NULL) free(argv[j]);
		if(NULL != binary) free(binary);
		if(NULL != argv) free(argv);
		if(error) goto ERR;
	}

	/* Second, we need to setup the input and output ports */
	runtime_api_pipe_id_t input_pid = runtime_stab_get_pipe(servlet_ids[header.input_node], input_port);
	if(ERROR_CODE(runtime_api_pipe_id_t) == input_pid)
		ERROR_LOG_GOTO(ERR, "Cannot get the pipe port id for the input port: <NID=%u, Name=%s>", header.input_node, input_port);
	else
	{
		LOG_DEBUG("Input port ID: <NID=%u, PID=%u>", header.input_node, input_pid);
		free(input_port);
		input_port = NULL;
	}

	runtime_api_pipe_id_t output_pid = runtime_stab_get_pipe(servlet_ids[header.output_node], output_port);
	if(ERROR_CODE(runtime_api_pipe_id_t) == output_pid)
		ERROR_LOG_GOTO(ERR, "Cannot get the pipe port id for the output port: <NID=%u, Name=%s>", header.output_node, output_port);
	else
	{
		LOG_DEBUG("Output port ID: <NID%u, PID=%u>", header.output_node, output_pid);
		free(output_port);
		output_port = NULL;
	}

	if(ERROR_CODE(int) == sched_service_buffer_set_input(buffer, header.input_node, input_pid))
		ERROR_LOG_GOTO(ERR, "Cannot set the input port");

	if(ERROR_CODE(int) == sched_service_buffer_set_output(buffer, header.output_node, output_pid))
		ERROR_LOG_GOTO(ERR, "Cannot set the output port");

	/* Finally, do the interconnections */
	for(i = 0; i < header.edge_count; i ++)
	{
		uint32_t nids[2];
		char* from_port = NULL, *to_port = NULL;
		runtime_api_pipe_id_t from_pid, to_pid;

		if(ERROR_CODE(int) == _fd_io(fd, nids, sizeof(nids), _READ))
			ERROR_LOG_GOTO(ERR, "Cannot read the NIDs from FD");

		if(NULL == (from_port = _read_string(fd)))
			ERROR_LOG_GOTO(ERR, "Cannot read the from port from FD");

		if(ERROR_CODE(runtime_api_pipe_id_t) == (from_pid = runtime_stab_get_pipe(servlet_ids[nids[0]], from_port)))
			ERROR_LOG_GOTO(EDGE_ERR, "Cannot get the PID for the pipe: <NID=%u, Name=%s>", nids[0], from_port);
		else
		{
			free(from_port);
			from_port = NULL;
		}

		if(NULL == (to_port = _read_string(fd)))
			ERROR_LOG_GOTO(EDGE_ERR, "Cannot read the to port from FD");

		if(ERROR_CODE(runtime_api_pipe_id_t) == (to_pid = runtime_stab_get_pipe(servlet_ids[nids[1]], to_port)))
			ERROR_LOG_GOTO(EDGE_ERR, "Cannot get the PID for the pipe: <NID=%u, Name=%s>", nids[1], to_port);
		else
		{
			free(to_port);
			to_port = NULL;
		}

		sched_service_pipe_descriptor_t pipeline = {
			.source_node_id        = (sched_service_node_id_t)nids[0],
			.destination_node_id   = (sched_service_node_id_t)nids[1],
			.source_pipe_desc      = from_pid,
			.destination_pipe_desc = to_pid
		};

		if(ERROR_CODE(int) == sched_service_buffer_add_pipe(buffer, pipeline))
			ERROR_LOG_GOTO(EDGE_ERR, "Cannot add pipe to the service buffer: <NID=%u,PID=%u> -> <NID=%u,PID=%u>", nids[0], from_pid, nids[1], to_pid);
		else
			LOG_DEBUG("Add new pipe to service buffer: <NID=%u,PID=%u> -> <NID=%u,PID=%u>", nids[0], from_pid, nids[1], to_pid);
		continue;
EDGE_ERR:
		if(NULL != from_port) free(from_port);
		if(NULL != to_port) free(to_port);
		goto ERR;
	}

	/* The last step, we could build the service based on the buffer */

	if(NULL == (ret = sched_service_from_buffer(buffer)))
		ERROR_LOG_GOTO(ERR, "Cannot build service from the service buffer");

	if(ERROR_CODE(int) == sched_service_buffer_free(buffer))
		LOG_WARNING("Cannot dispose the used service buffer");

	free(servlet_ids);

	return ret;
ERR:
	if(NULL != buffer) sched_service_buffer_free(buffer);
	if(NULL != input_port) free(input_port);
	if(NULL != output_port) free(output_port);
	if(NULL != servlet_ids) free(servlet_ids);
	return NULL;
}

int sched_service_dump_fd(const sched_service_t* service, int fd)
{
	if(NULL == service || fd < 0)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_dump_header_t header = {
		.node_count = (uint32_t)service->node_count,
		.edge_count = 0,
		.input_node = service->input_node,
		.output_node = service->output_node
	};

	/* Then we need to compute the total number of header */
	uint32_t i;
	for(i = 0; i < header.node_count; i ++)
		header.edge_count += service->nodes[i]->incoming_count;

	const runtime_pdt_t* input_pdt = runtime_stab_get_pdt(service->nodes[service->input_node]->servlet_id);
	if(NULL == input_pdt) ERROR_RETURN_LOG(int, "Cannot get the PDT for the input node");

	const runtime_pdt_t* output_pdt = runtime_stab_get_pdt(service->nodes[service->output_node]->servlet_id);
	if(NULL == output_pdt) ERROR_RETURN_LOG(int, "Cannot get the PDT for the output node");

	const char* input_port = runtime_pdt_get_name(input_pdt, service->input_pipe);
	if(NULL == input_port) ERROR_RETURN_LOG(int, "Cannot get the input port name");

	const char* output_port = runtime_pdt_get_name(output_pdt, service->output_pipe);
	if(NULL == output_port) ERROR_RETURN_LOG(int, "Cannot get the output port name");

	LOG_DEBUG("Dump header: <Node_Count=%u, Edge_Count=%u, Input_Node=%u, Output_Node=%u, Input_Port=%s, Output_Port=%s>",
	          header.node_count, header.edge_count, header.input_node, header.output_node, input_port, output_port);

	/* Then we dump the header first */
	if(ERROR_CODE(int) == _fd_io(fd, &header, sizeof(header), _WRITE))
		ERROR_RETURN_LOG(int, "Cannot write the service header to FD");

	if(ERROR_CODE(int) == _dump_string(fd, input_port))
		ERROR_RETURN_LOG(int, "Cannot write the input port name to FD");

	if(ERROR_CODE(int) == _dump_string(fd, output_port))
		ERROR_RETURN_LOG(int, "Cannot write the output port name to FD");

	/* Dump the node definition */
	for(i = 0; i < header.node_count; i ++)
	{
		uint32_t argc;
		char const* const* argv;

		const  char* binary_path = runtime_stab_get_binary_path(service->nodes[i]->servlet_id);
		if(NULL == binary_path)
			ERROR_RETURN_LOG(int, "Cannot get the binary path of the servlet (ID=%u)", service->nodes[i]->servlet_id);

		if(ERROR_CODE(int) == _dump_string(fd, binary_path))
			ERROR_RETURN_LOG(int, "Cannot dump the servlet binary to the FD");
		else
			LOG_DEBUG("Servlet Binary Path = %u", service->nodes[i]->servlet_id);

		if(NULL == (argv = sched_service_get_node_args(service, i, &argc)))
			ERROR_RETURN_LOG(int, "Cannot get the initialization arguments for the servlet");

		if(ERROR_CODE(int) == _fd_io(fd, &argc, sizeof(argc), _WRITE))
			ERROR_RETURN_LOG(int, "Cannot write the argument count to fd for servlet %u", i);

		uint32_t j;
		for(j = 0; j < argc; j ++)
		{
			if(ERROR_CODE(int) == _dump_string(fd, argv[j]))
				ERROR_RETURN_LOG(int, "Cannot write the servlet argument string to the FD servlet id %u", i);
		}
	}

	/* Finally we dump the edge definitons */
	for(i = 0 ; i < header.node_count; i ++)
	{
		uint32_t j;
		for(j = 0; j < service->nodes[i]->incoming_count; j ++)
		{
			sched_service_pipe_descriptor_t pipeline = service->nodes[i]->incoming[j];

			const runtime_pdt_t* src_pdt = runtime_stab_get_pdt(service->nodes[pipeline.source_node_id]->servlet_id);
			if(NULL == src_pdt)
				ERROR_RETURN_LOG(int, "Cannot get the source PDT");

			const runtime_pdt_t* dst_pdt = runtime_stab_get_pdt(service->nodes[pipeline.destination_node_id]->servlet_id);
			if(NULL == dst_pdt)
				ERROR_RETURN_LOG(int, "Cannot get the source or destination PDT");

			const char* src_port = runtime_pdt_get_name(src_pdt, pipeline.source_pipe_desc);
			if(NULL == src_port)
				ERROR_RETURN_LOG(int, "Cannot get the source port name");

			const char* dst_port = runtime_pdt_get_name(dst_pdt, pipeline.destination_pipe_desc);
			if(NULL == dst_port)
				ERROR_RETURN_LOG(int, "Cannot get the destination port name");

			LOG_DEBUG("Adding edge defitiontion to dump: <source: %u:%s, destionation: %u:%s>",
			          pipeline.source_node_id, src_port,
			          pipeline.destination_node_id, dst_port);

			uint32_t nids[2] = { [0] = (uint32_t)pipeline.source_node_id,
				                 [1] = (uint32_t)pipeline.destination_node_id};

			if(ERROR_CODE(int) == _fd_io(fd, nids, sizeof(nids), _WRITE))
				ERROR_RETURN_LOG(int, "Cannot write node IDs to the FD");

			if(ERROR_CODE(int) == _dump_string(fd, src_port))
				ERROR_RETURN_LOG(int, "Cannot dump the source port name to the FD");

			if(ERROR_CODE(int) == _dump_string(fd, dst_port))
				ERROR_RETURN_LOG(int, "Cannot dump the destionation port name to FD");
		}
	}
	return 0;
}
