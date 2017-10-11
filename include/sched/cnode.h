/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The critical node analyzer used for task cancellation optimization
 * @details The concept of critical node is simple. A node, or a servlet is
 *          critical, if and only if once the node gets cancelled, there's
 *          at least one other node will be definitely cancelled. <br/>
 *          Translate this definition to a graph theory term, a critical node
 *          is a node in a ADG removing which will cause at least one other node
 *          not reachable. <br/>
 *          The following two conditions are equavilent:
 *          <ol>
 *          	<li>Node A is critical</li>
 *          	<li>Exist a node B which &lt;A,B&gt; is in the edge set and deg_in(B)=1</li>
 *          </ol>
 *          <p>
 *          The condition 2 =&gt; condition 1 is quite stright forward, since if condition 2 is true
 *          it means by removing the node A, the node B will become a isolated node which is definitely
 *          not reachable. <br/>
 *          </p>
 *          <p>
 *          The condition 1 =&gt; condition 2 is a little bit complicated.<br/>
 *          Assume C(A) is the set of nodes which will be not reachable after node A removal.<br/>
 *          If there's no node in C(A) connected to node A and the in-degree is 1, so we have deg_in(C) &gt; 1
 *          for all the node C in C(A) which has edge between A and C. <br/>
 *          At the same time, for all nodes D in C(A) which doesn't have edge connected to A, because of
 *          the reacheablity, it's obvious that deg_in(D) &gt; 0. <br/>
 *          Now we remove the node A, since each node that is connected to A will decrease the in-degree by 1,
 *          so we havae deg_in'(C) &gt; 0 for all the C that is used to have an edge with A. <br/>
 *          At the same time, all the nodes D which doesn't have edge connected to A, won't be affected, so deg_in'(D) &gt; 0.<br/>
 *          <br/>
 *          Since C(A) is a subgraph of the entire ADG, it's not possible that deg_in'(N) &gt; 0 for all the N in C(A). <br/>
 *          So we have proven that condition 2 also implies condition 1. <br/>
 *          </p>
 *          <br/>
 *          Based on the proof above, we have shown condition 1 and contidion 2 are equavilent. <br/>
 *			Which means we are able to identify all the critical node by the destination's in degree.
 *
 *			<br/>
 *			We also define the node set C(A) as the cluster of critical node A.
 *			And all the node comes out from the C(A) is defined as boundary of the cluster A.
 * @file sched/cnode.h
 **/
#include <utils/static_assertion.h>
#ifndef __PLUMBER_SCHED_CNODE_H__
#define __PLUMBER_SCHED_CNODE_H__
/**
 * @brief the type used to describe the destination of a output pipe
 **/
typedef struct {
	sched_service_node_id_t node_id;      /*!< the node id for the input end*/
	runtime_api_pipe_id_t   pipe_desc;    /*!< the pipe id for the input end*/
} sched_cnode_edge_dest_t;

/**
 * @brief the type used to describe the boundary edge set of the critical cluster
 **/
typedef struct {
	uint32_t                    output_cancelled:1;/*!<indicates if the output of the entire service graph will be cancelled */
	uint32_t                    count;             /*!< the number of the edges */
	uintpad_t                   __padding__[0];
	sched_cnode_edge_dest_t     dest[0];           /*!< the actual destination list */
} sched_cnode_boundary_t;
STATIC_ASSERTION_LAST(sched_cnode_boundary_t, dest);
STATIC_ASSERTION_SIZE(sched_cnode_boundary_t, dest, 0);

/**
 * @brief the critical node information
 **/
struct _sched_cnode_info_t {
	const sched_service_t*  service;       /*!< the service has been analyzed */
	uintpad_t               __padding__[0];
	sched_cnode_boundary_t* boundary[0];   /*!< the critical node boundaries */
};
STATIC_ASSERTION_LAST(sched_cnode_info_t, boundary);
STATIC_ASSERTION_SIZE(sched_cnode_info_t, boundary, 0);

/**
 * @brief analyze a service graph for the critical node
 * @param service the service to analyze
 * @return the analyze result, NULL indicates an error
 **/
sched_cnode_info_t* sched_cnode_analyze(const sched_service_t* service);

/**
 * @brief dispose a used critical node information
 * @param info the cnode info to dispose
 * @return status code
 **/
int sched_cnode_info_free(sched_cnode_info_t* info);

/**
 * @brief get the cluster information on critical node, if the node is not critical, return NULL
 * @param info the cnode info to query
 * @param node the target node
 * @return the node info, NULL if not found
 **/
static inline const sched_cnode_boundary_t* sched_cnode_info_get_boundary(const sched_cnode_info_t* info, sched_service_node_id_t node)
{
	if(NULL == info) return NULL;
	size_t size = sched_service_get_num_node(info->service);
	if(node >= size) return NULL;
	return info->boundary[node];
}

#endif /* __PLUMBER_SCHED_CNODE_H__ */
