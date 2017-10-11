/**
 * Copyright (C) 2017, Kejun Li
 * Copyright (C) 2017, Hao Hou
 **/
#include <utils/log.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <utils/mempool/oneway.h>
#include <utils/static_assertion.h>
#include <error.h>

#define IDX(head,skew) (head+skew)

/**
 * @brief a internal node
 **/
typedef struct _mem_node_t {
	size_t size; /*!< size of the current node */
	size_t usedsize; /*!< size that already allocated */
	struct _mem_node_t* next;
	uintpad_t __padding__[0];
	char data[0]; /*!< data section */
}_mem_node_t;
STATIC_ASSERTION_LAST(_mem_node_t, data);
STATIC_ASSERTION_SIZE(_mem_node_t, data, 0);

/**
 * @brief the data structure for a memory pool
 **/
struct _mempool_oneway_t {
	size_t size; /*!< size of the table, in this case, it will only increment.  */
	_mem_node_t* nodes; /*!< the node linked list */
};

/**
 * @note This is used to initialize the new memory node.
 **/
static inline _mem_node_t* _mem_node_new(size_t size)
{
	_mem_node_t* newnode = (_mem_node_t*)malloc(sizeof(_mem_node_t) + size);

	if(NULL == newnode)
	    ERROR_PTR_RETURN_LOG_ERRNO("cannot allocate memory for new mem node.");

	newnode->size = size;
	newnode->usedsize = 0;
	newnode->next = NULL;
	return newnode;
}

// Initialize a new mem table with a node size of MEM_POOL_NODE_INITIAL_SIZE
mempool_oneway_t* mempool_oneway_new(size_t init_size)
{
	mempool_oneway_t* newtable = (mempool_oneway_t*)malloc(sizeof(mempool_oneway_t));

	if(NULL == newtable)
	    ERROR_PTR_RETURN_LOG_ERRNO("cannot allocate memory for new mem table.");

	if(NULL == (newtable->nodes = _mem_node_new(init_size)))
	    ERROR_LOG_GOTO(ERR, "cannot allocate the intial memory node");

	newtable->size = init_size;

	return newtable;
ERR:
	if(newtable != NULL)
	{
		if(newtable->nodes != NULL) free(newtable->nodes);
	}
	return 0;
}

void* mempool_oneway_alloc(mempool_oneway_t* pool, size_t size)
{
	size_t remainsize = pool->nodes->size - pool->nodes->usedsize;
	void* data = NULL;

	_mem_node_t* newnode = NULL;
	size_t newnodesize = pool->size;

	if(0 == size)
	    ERROR_PTR_RETURN_LOG("invalid size input 0.");

	if(size > remainsize)
	{
		if(size > pool->size)
		    newnodesize = size;

		newnode = _mem_node_new(newnodesize);

		if(NULL == newnode)
		    ERROR_PTR_RETURN_LOG("cannot create new mem node");

		pool->size += newnodesize;
		newnode->next = pool->nodes;
		pool->nodes = newnode;
	}

	data = (void*) IDX( pool->nodes->data, pool->nodes->usedsize );
	pool->nodes->usedsize += size;
	return data;
}

int mempool_oneway_free(mempool_oneway_t* pool)
{
	if(NULL == pool) ERROR_RETURN_LOG(int, "invalid arguments");

	_mem_node_t* head = pool->nodes;
	_mem_node_t* node = NULL;
	while(head != NULL)
	{
		node = head;
		head = head->next;
		free(node);
	}
	free(pool);

	return 0;
}
