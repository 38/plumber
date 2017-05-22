/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

#include <utils/log.h>

/**
 * @brief read from the bit map
 * @param arr the bitmap array
 * @param n the number of the bitmap
 * @return the read result
 **/
static inline int _bitmap_get(uint64_t* arr, uint32_t n)
{
	uint32_t ofs = n / 64;
	return (arr[ofs] & (1ull << (n % 64))) != 0;
}

/**
 * @brief flip from the bit map
 * @param arr the bitmap array
 * @param n the number of bitmap
 * @return nothing
 **/
static inline void _bitmap_flip(uint64_t* arr, uint32_t n)
{
	uint32_t ofs = n / 64;
	arr[ofs] ^= (1ull << (n % 64));
}

/**
 * @brief DFS the service graph, and assume the node removal has been removed.
 * @note if removal is error code, it won't macth any node in the service graph, which means we
 *       are not remomving any node.
 *       If the node is reached, the state bit will flip, so after two BFS one removing the critical node
 *       another one doesn't. After that if the state is 1 means this node is belongs to the cluster
 * @param service the target service graph
 * @param size the number of nodes in the graph
 * @param state the state bit map
 * @param removal the node to remove
 * @return status code
 **/
static inline int _service_dfs(const sched_service_t* service, size_t size, uint64_t* state, sched_service_node_id_t removal)
{
	sched_service_node_id_t input = sched_service_get_input_node(service);
	if(ERROR_CODE(sched_service_node_id_t) == input)
	    ERROR_RETURN_LOG(int, "Cannot get the input node of the service");

	if(input == removal) return 0;

	int rc = 0;
	uint64_t* visited = (uint64_t*)calloc(1, sizeof(uint64_t) * ((size + 63) / 64));
	if(NULL == visited)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the visited flag");

	sched_service_node_id_t* stack = (sched_service_node_id_t*)malloc(sizeof(sched_service_node_id_t) * size);
	if(NULL == stack)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the visit stack");

	uint32_t sp = 1;
	if(ERROR_CODE(sched_service_node_id_t) == (stack[0] = sched_service_get_input_node(service)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the input node of the service");

	if(removal != ERROR_CODE(sched_service_node_id_t) && removal < sched_service_get_num_node(service))
	    _bitmap_flip(visited, removal);

	_bitmap_flip(visited, stack[0]);
	_bitmap_flip(state, stack[0]);


	while(sp > 0)
	{
		sched_service_node_id_t current = stack[--sp];

		const sched_service_pipe_descriptor_t* pds;
		uint32_t count, i;

		if(NULL == (pds = sched_service_get_outgoing_pipes(service, current, &count)))
		    ERROR_LOG_GOTO(ERR, "Cannot get the outgoing pipes from the service graph for node %u", current);

		for(i = 0; i < count; i ++)
		    if(!_bitmap_get(visited, pds[i].destination_node_id))
		    {
			    stack[sp++] = pds[i].destination_node_id;
			    _bitmap_flip(visited, pds[i].destination_node_id);
			    _bitmap_flip(state, pds[i].destination_node_id);
		    }
	}

	goto CLEANUP;

ERR:
	rc = ERROR_CODE(int);

CLEANUP:

	if(NULL != visited) free(visited);
	if(NULL != stack) free(stack);

	return rc;
}
/**
 * @brief make a new boundary array for the critical node
 * @param service the target service
 * @param num_nodes the number of nodes in this service graph
 * @param cnode the critical node
 * @param state the buffer used to store the state bits
 * @param state_size the size of the state
 * @return the newly created boundary object or NULL on error
 **/
sched_cnode_boundary_t* _boundary_new(const sched_service_t* service, size_t num_nodes, sched_service_node_id_t cnode, uint64_t* state, size_t state_size)
{
	sched_service_node_id_t i;
	uint32_t j, size;
	const sched_service_pipe_descriptor_t* outputs;
	sched_service_node_id_t output = sched_service_get_output_node(service);
	if(ERROR_CODE(sched_service_node_id_t) == output) ERROR_PTR_RETURN_LOG("Cannot get the output node of the service");

	memset(state, 0, state_size);

	/* Make two DFS, so that we can identify the cluster of this critical node */
	if(ERROR_CODE(int) == _service_dfs(service, num_nodes, state, ERROR_CODE(sched_service_node_id_t)) ||
	   ERROR_CODE(int) == _service_dfs(service, num_nodes, state, cnode))
	    ERROR_PTR_RETURN_LOG("Cannot DFS the service graph");

	/* Create the new boundary list */
	size_t capacity = SCHED_CNODE_BOUNDARY_INIT_SIZE;
	size_t array_size = sizeof(sched_cnode_boundary_t) + sizeof(sched_cnode_edge_dest_t) * SCHED_CNODE_BOUNDARY_INIT_SIZE;
	sched_cnode_boundary_t* ret = (sched_cnode_boundary_t*)malloc(array_size);

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the boundary array");
	ret->count = 0;

	/* Check each node for the cluster mark */
	for(i = 0; i < num_nodes; i ++)
	    if(_bitmap_get(state, i))
	    {
		    if(NULL == (outputs = sched_service_get_outgoing_pipes(service, i, &size)))
		        ERROR_LOG_GOTO(ERR, "Cannot get the outgoing pipes for service node %u", i);

		    for(j = 0; j < size; j ++)
		        if(!_bitmap_get(state, outputs[j].destination_node_id))
		        {
			        /* This means the edge cross the boundary of the cluster */
			        if(capacity <= ret->count)
			        {
				        LOG_DEBUG("The boundary array contains more than %zu elements, resize to %zu", capacity, capacity * 2);
				        sched_cnode_boundary_t* new = (sched_cnode_boundary_t*)realloc(ret, array_size * 2);
				        if(NULL == new) ERROR_LOG_GOTO(ERR, "Cannot resize the boundary array");
				        ret = new;
				        capacity *= 2;
				        array_size *= 2;
			        }

			        ret->dest[ret->count].node_id = outputs[j].destination_node_id;
			        ret->dest[ret->count].pipe_desc = outputs[j].destination_pipe_desc;
			        LOG_DEBUG("Found bound pipe for critical node cluster of servlet %u: <NID=%u, PID=%u>",
			                  cnode, ret->dest[ret->count].node_id, ret->dest[ret->count].pipe_desc);
			        ret->count ++;
		        }
	    }
	ret->output_cancelled = (_bitmap_get(state, output) != 0);
	return ret;

ERR:
	if(ret != NULL) free(ret);
	return NULL;
}

sched_cnode_info_t* sched_cnode_analyze(const sched_service_t* service)
{
	uint64_t* state = NULL;
	size_t state_size;
	sched_service_node_id_t nid;
	uint32_t i;
	if(NULL == service) ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t num_nodes = sched_service_get_num_node(service);
	if(ERROR_CODE(size_t) == num_nodes) ERROR_PTR_RETURN_LOG("Cannot get the number of node in the service graph");

	sched_cnode_info_t* ret = (sched_cnode_info_t*)calloc(1, sizeof(sched_cnode_info_t) + sizeof(sched_cnode_boundary_t*) * num_nodes);
	if(NULL == ret) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the critical node analysis result");

	state_size = sizeof(uint64_t) * ((num_nodes + 63) / 64);

	state = (uint64_t*)malloc(state_size);
	if(NULL == state) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the state array");

	for(nid = 0; nid < num_nodes; nid ++)
	{
		const sched_service_pipe_descriptor_t* pds;
		uint32_t flag = 1, in_deg;

		if(NULL == (pds = sched_service_get_incoming_pipes(service, nid, &in_deg)))
		    ERROR_LOG_GOTO(ERR, "Cannot get the incoming pipes for node %u", nid);

		/* Check if the node is a critical node based on it's in-degree */
		if(in_deg == 0) continue;
		sched_service_node_id_t src_node = pds[0].source_node_id;
		for(i = 0; flag && i < in_deg; i ++)
		    flag = (src_node == pds[i].source_node_id);
		if(!flag) continue;

		sched_service_node_id_t cnode = pds[0].source_node_id;
		if(ret->boundary[cnode] == NULL)
		{
			LOG_DEBUG("Found critical node 0x%x", cnode);
			if(NULL == (ret->boundary[cnode] = _boundary_new(service, num_nodes, cnode, state, state_size)))
			    ERROR_LOG_GOTO(ERR, "Cannot create boundary array for critical node %u", cnode);
		}
	}

	ret->service = service;
	free(state);

	return ret;
ERR:
	for(i = 0; i < num_nodes; i ++)
	    if(ret->boundary[i] != NULL)
	        free(ret->boundary[i]);
	free(ret);
	if(NULL != state) free(state);
	return NULL;

}

int sched_cnode_info_free(sched_cnode_info_t* info)
{
	if(NULL == info) ERROR_RETURN_LOG(int, "Invalid arguments");

	size_t i;
	size_t size = sched_service_get_num_node(info->service);
	if(ERROR_CODE(size_t) == size) ERROR_RETURN_LOG(int, "Cannot get the number of nodes in the service graph");

	for(i = 0; i < size; i ++)
	    if(info->boundary[i] != NULL)
	        free(info->boundary[i]);

	free(info);

	return 0;
}
