/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>

/**
 * @brief The copy-on-write tree node
 **/
typedef struct _node_t {
	uint32_t                 refcnt;      /*!< The reference counter for this node */
	pss_value_t              value;       /*!< The value data */
	uintpad_t                __padding__[0];
	struct {
		struct _node_t*      left;         /*!< The left child */
		struct _node_t*      right;        /*!< The right child */
	}                        child[0];     /*!< The non-leaf node data */
} _node_t;
STATIC_ASSERTION_LAST(_node_t, child);
STATIC_ASSERTION_SIZE(_node_t, child, 0);

/**
 * @brief A copy-on-write stack frame
 **/
struct _pss_frame_t {
	_node_t*   root;    /*!< The root of the tree */
};

/**
 * @brief Create a new node
 * @param leaf If this node is a leaf node
 * @return The newly created node
 **/
static inline _node_t* _node_new(int leaf)
{
	size_t size = sizeof(_node_t) + (leaf?0:sizeof(((_node_t*)NULL)->child[0]));
	_node_t* ret = (_node_t*)calloc(1, size);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new node");

	return ret;
}

/**
 * @brief Get a node from the COW tree
 * @param root The root node
 * @param left The left boundary of the register range
 * @param right The right boundary of the register range
 * @param target The target register ID we are looking for
 * @return The reference to the node, if this function returns NULL, it means the register haven't been intialized yet
 **/
static inline const _node_t* _cow_get(_node_t* root, pss_bytecode_regid_t left, pss_bytecode_regid_t right, pss_bytecode_regid_t target)
{
	for(;NULL != root;)
	{
		pss_bytecode_regid_t mid = (pss_bytecode_regid_t)(((uint32_t)left + (uint32_t)right) / 2);
		if(mid == target) return root;

		if(target < mid) right = mid, root = root->child->left;
		if(mid < target) left = (pss_bytecode_regid_t)(mid + 1), root = root->child->right;
	}

	return root;
}

/**
 * @brief Prepare to modify a register
 * @note  This is the key function of Copy-On-Write frame, if the node is used by multiple frames,
 *        the root node will be copied
 * @param root  The root node
 * @param left  The left boundary of the register id
 * @param right The right boundary of the register id
 * @param target The register id we want to modify
 * @param value The new value to write
 * @return The tree that has been created after modify
 **/
static inline _node_t* _cow_write(_node_t* root, pss_bytecode_regid_t left, pss_bytecode_regid_t right, pss_bytecode_regid_t target, pss_value_t value)
{
	_node_t* ret = root = (root == NULL)?_node_new(0):root;

	_node_t** parent = &ret;

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new node");

	/* If this node is just created, we need to make it pointed by the frame */
	if(ret->refcnt == 0) ret->refcnt = 1;

	for(;;)
	{
		pss_bytecode_regid_t mid = (pss_bytecode_regid_t)(((uint32_t)left + (uint32_t)right) / 2);

		/* If the target is not current root, we need to make sure its children are properly initialized */
		if((mid & 1) && root->child->left == NULL)
		{
			if(root->child->right != NULL) ERROR_PTR_RETURN_LOG("Code bug: unevenly initialized left child and right child");

			if(NULL == (root->child->left = _node_new(right - left == 3)))
			    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the left child");

			if(NULL == (root->child->right = _node_new(right - left == 3)))
			    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the right child");

			root->child->left->refcnt = root->child->right->refcnt = 1;
		}

		if(root->refcnt > 1)
		{
			/* If the root node have more than one reference, we need to copy the node */

			_node_t* node = _node_new(right - left == 1);

			if(ERROR_CODE(int) == pss_value_incref(node->value = root->value))
			    ERROR_PTR_RETURN_LOG("Cannot increase the ref counter of the value");

			node->refcnt = 1;
			root->refcnt --;
			if(right - left > 1)
			{
				node->child[0] = root->child[0];
				root->child->left->refcnt ++;
				root->child->right->refcnt ++;
			}

			root = node;
		}

		*parent = root;

		if (target < mid)
		    right = mid, parent = &root->child->left, root = root->child->left;
		else if (mid < target)
		    left = (pss_bytecode_regid_t)(mid + 1u), parent = &root->child->right, root = root->child->right;
		else  break;
	}

	if(ERROR_CODE(int) == pss_value_decref(root->value))
	    ERROR_PTR_RETURN_LOG("Cannot decrease the ref counter of the previous value");

	if(ERROR_CODE(int) == pss_value_incref(root->value = value))
	    ERROR_PTR_RETURN_LOG("Cannot increase the ref counter of the value");

	return ret;
}

/**
 * @brief decreference a entire tree
 * @param root The tree node
 * @param left The smallest register id
 * @param right The largest register id + 1
 * @return status code
 **/
static inline int _tree_decref(_node_t* root, pss_bytecode_regid_t left, pss_bytecode_regid_t right)
{
	if(NULL == root) return 0;

	int rc = 0;

	if(root->refcnt > 0) root->refcnt --;

	if(root->refcnt == 0)
	{
		if(right - left > 1)
		{
			pss_bytecode_regid_t mid = (pss_bytecode_regid_t)(((uint32_t)left + (uint32_t)right) / 2);
			if(ERROR_CODE(int) == _tree_decref(root->child->left, left, mid))
			{
				LOG_ERROR("Cannot decref the left child of the tree");
				rc = ERROR_CODE(int);
			}

			if(ERROR_CODE(int) == _tree_decref(root->child->right, (pss_bytecode_regid_t)(mid + 1), right))
			{
				LOG_ERROR("Cannot decref the right child of the tree");
				rc = ERROR_CODE(int);
			}
		}
		if(ERROR_CODE(int) == pss_value_decref(root->value))
		{
			LOG_ERROR("Cannot decref the register value");
			rc = ERROR_CODE(int);
		}
		free(root);
	}

	return rc;
}

pss_frame_t* pss_frame_new(const pss_frame_t* from)
{
	pss_frame_t* ret = (pss_frame_t*)malloc(sizeof(pss_frame_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the frame");

	ret->root = (NULL == from ? NULL : from->root);

	if(NULL != ret->root) ret->root->refcnt ++;

	return ret;
}

int pss_frame_free(pss_frame_t* frame)
{
	if(NULL == frame) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = _tree_decref(frame->root, (pss_bytecode_regid_t)0, (pss_bytecode_regid_t)-1);

	free(frame);

	return rc;
}

pss_value_t pss_frame_reg_get(const pss_frame_t* frame, pss_bytecode_regid_t regid)
{
	pss_value_t ret = {};

	if(NULL == frame || ERROR_CODE(pss_bytecode_regid_t) == regid)
	{
		ret.kind = PSS_VALUE_KIND_ERROR;
		return ret;
	}

	const _node_t* node;

	if(NULL != (node = _cow_get(frame->root, (pss_bytecode_regid_t)0, (pss_bytecode_regid_t)-1, regid)))
	    ret = node->value;

	return ret;
}

int pss_frame_reg_set(pss_frame_t* frame, pss_bytecode_regid_t regid, pss_value_t value)
{
	if(NULL == frame || ERROR_CODE(pss_bytecode_regid_t) == regid || value.kind == PSS_VALUE_KIND_ERROR)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_node_t* node = _cow_write(frame->root, (pss_bytecode_regid_t)0, (pss_bytecode_regid_t)-1, regid, value);

	if(NULL == node)
	    ERROR_RETURN_LOG(int, "Cannot write the frame to register");

	frame->root = node;

	return 0;
}

int pss_frame_reg_move(pss_frame_t* frame, pss_bytecode_regid_t from, pss_bytecode_regid_t to)
{
	pss_value_t value  = pss_frame_reg_get(frame, from);

	if(ERROR_CODE(int) == pss_frame_reg_set(frame, to, value))
	    ERROR_RETURN_LOG(int, "Cannot move the register values");

	return 0;
}
