/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <proto.h>
#include <proto/cache.h>

#include <sandbox.h>

#include <error.h>
#include <log.h>

/**
 * @brief represent a pending operation
 **/
typedef struct _op_t {
	proto_type_t*            type_obj;    /*!< the type object, for deletion operation, this must be NULL */
	proto_type_t*            removed;     /*!< the removed type object, it may used in the cache, so we cant dispose at once */
	int                      is_update;   /*!< if this is an update */
	char*                    target;      /*!< the target path */
	struct _op_t*            next;        /*!< the next pointer in the linked list */
} _op_t;

/**
 * @brief the sandbox data structure
 **/
struct _sandbox_t {
	uint32_t               validated:1;/*!< if this sandbox has been validated */
	uint32_t               posted:1;   /*!< if this sandbox has been posted to the system */
	_op_t*                 oplist;     /*!< the pending operations */
	_op_t*                 secondary;  /*!< the secondary operation list, which is triggered by the operations in the operation list */
	sandbox_insert_flags_t flags;      /*!< the sandbox flags */
};

sandbox_t* sandbox_new(sandbox_insert_flags_t flags)
{
	sandbox_t* ret = (sandbox_t*)malloc(sizeof(*ret));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the sandbox");

	ret->oplist = NULL;
	ret->secondary = NULL;
	ret->flags = flags;
	ret->validated = 0;
	ret->posted = 0;
	return ret;
}

/**
 * @brief dispose a list of opreations
 * @param oplist the operation list to dispose
 * @return status code
 **/
static inline int _dispose_oplist(_op_t* oplist)
{
	int rc = 0;
	while(NULL != oplist)
	{
		_op_t* cur = oplist;
		oplist = oplist->next;

		if(NULL != cur->type_obj && ERROR_CODE(int) == proto_type_free(cur->type_obj))
		{
			log_libproto_error(__FILE__, __LINE__);
			rc = ERROR_CODE(int);
		}

		if(NULL != cur->removed && ERROR_CODE(int) == proto_type_free(cur->removed))
		{
			log_libproto_error(__FILE__, __LINE__);
			rc = ERROR_CODE(int);
		}

		if(NULL != cur->target)
		    free(cur->target);

		free(cur);
	}

	return rc;
}

int sandbox_free(sandbox_t* sandbox)
{
	if(NULL == sandbox)
	    ERROR_RETURN_LOG(int, "Invaild arguments");

	int rc = 0;

	if(ERROR_CODE(int) == _dispose_oplist(sandbox->oplist))
	    rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == _dispose_oplist(sandbox->secondary))
	    rc = ERROR_CODE(int);

	free(sandbox);

	return rc;
}
/**
 * @brief create a new operation object
 * @param typename the typename for the operation
 * @return the newly created object, NULL on error
 * @note this function assume the typename is a valid pointer
 **/
static inline _op_t* _new_op_node(const char* typename)
{
	_op_t* ret;

	if(NULL == (ret = (_op_t*)calloc(1, sizeof(*ret))))
	    ERROR_PTR_RETURN_LOG_ERRNO("Canont allocate memory for the sandbox operation");

	size_t name_length = strlen(typename);

	if(NULL == (ret->target = (char*)malloc(sizeof(ret->target[0]) * (name_length + 1))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the target type name for the sandbox operation");
	memcpy(ret->target, typename, name_length + 1);

	return ret;
ERR:
	if(ret != NULL)
	{
		if(ret->target != NULL) free(ret->target);
		free(ret);
	}
	return NULL;
}
/**
 * @brief get the pending operation on the given typename
 * @param sandbox the sandbox to search
 * @param typename the name of the type
 * @return the pending operation on the typename, if it's not exist, create a new one, NULL is error case
 **/
static inline _op_t* _get_op_node(sandbox_t* sandbox, const char* typename)
{
	_op_t* ret;
	for(ret = sandbox->oplist; NULL != ret && strcmp(ret->target, typename) != 0; ret = ret->next);

	if(NULL == ret)
	{
		if(NULL == (ret = _new_op_node(typename)))
		    ERROR_PTR_RETURN_LOG("Cannot create pending operation object");

		ret->next = sandbox->oplist;
		sandbox->oplist = ret;
	}

	return ret;
}

int sandbox_insert_type(sandbox_t* sandbox, const char* typename, proto_type_t* type)
{
	if(NULL == sandbox || NULL == typename || NULL == type)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int check_result, update;
	if(ERROR_CODE(int) == (check_result = proto_cache_full_type_name_exist(typename)))
	    LOG_LIBPROTO_ERROR_RETURN(int);

	if(check_result)
	{
		if(sandbox->flags == SANDBOX_INSERT_ONLY)
		    ERROR_RETURN_LOG(int, "Overriding existing typename %s is not allowed", typename);
		update = 1;
	}
	else update = 0;

	_op_t* op = _get_op_node(sandbox, typename);
	if(NULL == op) ERROR_RETURN_LOG(int, "Cannot get the operation object");

	if(sandbox->flags == SANDBOX_ALLOW_UPDATE || sandbox->flags == SANDBOX_FORCE_UPDATE)
	{
		if(op->type_obj != NULL && ERROR_CODE(int) != proto_type_free(op->type_obj))
		    LOG_LIBPROTO_ERROR_RETURN(int);
		op->type_obj = NULL;
	} else if(op->type_obj)
	    ERROR_RETURN_LOG(int, "Cannot override the exsiting type object %s", typename);

	op->type_obj = type;
	op->is_update = (update != 0);
	return 0;
}

int sandbox_delete_type(sandbox_t* sandbox, const char* typename)
{
	if(NULL == sandbox || NULL == typename)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_op_t* op = _get_op_node(sandbox, typename);

	if(NULL == op) ERROR_RETURN_LOG(int, "Cannot get the operation object");

	if(op->type_obj != NULL && proto_type_free(op->type_obj) != ERROR_CODE(int))
	{
		op->type_obj = NULL;
		LOG_LIBPROTO_ERROR_RETURN(int);
	}

	op->is_update = 0;

	return 0;
}

/**
 * @brief get the underlying operation on the given typename
 * @note this function is similar to the _get_op_node, but the only differece is this is the version that
 *       also search for the seconary operation list as well. When operation is not found, instead of creating
 *       a new node in the peding operation list, create the operation in the seconary operation list.
 * @param sandbox the sandbox to search
 * @param typename the name of the type
 * @param created the buffer used to return the value that indicates if this node is a newly created one
 * @return the pending operation on the typename, if it's not exist, create a new one, NULL is error case
 **/
static inline _op_t* _get_secondary_node(sandbox_t* sandbox, const char* typename, int* created)
{
	_op_t* ret;
	for(ret = sandbox->oplist; NULL != ret && strcmp(ret->target, typename) != 0; ret = ret->next);

	if(NULL == ret)
	    for(ret = sandbox->secondary; NULL != ret && strcmp(ret->target, typename) != 0; ret = ret->next);

	if(NULL == ret)
	{
		if(NULL == (ret = _new_op_node(typename)))
		    ERROR_PTR_RETURN_LOG("Cannot create pending operation object");

		ret->next = sandbox->secondary;
		sandbox->secondary = ret;
		*created = 1;
	}
	else *created = 0;

	return ret;
}
/**
 * @brief copy the reverse dependency array, this is needed because it's possible
 *        a node is deleted when the recursive remove is in process. And this will
 *        invalidate the original reverse dependency array. To avoid this, we need
 *        duplicate a reverse dependency array.
 * @param rdeps the reverse dependency array
 * @return the array or error code
 **/
static inline char** _dup_rdeps(char const* const* rdeps)
{
	uint32_t n, i;
	for(n = 0; rdeps[n] != NULL; n ++);
	char** ret = (char**)malloc(sizeof(ret[0]) * (n + 1));
	if(NULL == ret)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the reverse dependencies");

	for(i = 0; i < n; i ++)
	{
		size_t l = strlen(rdeps[i]);
		if(NULL == (ret[i] = (char*)malloc(sizeof(ret[i][0]) * (l + 1))))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for dependency %u", i);
		memcpy(ret[i], rdeps[i], l + 1);
	}

	ret[n] = NULL;

	return ret;
ERR:
	if(NULL != ret)
	{
		for(;i > 0; free(ret[--i]));
		free(ret);
	}
	return NULL;
}

/**
 * @brief dispose the reverse dependency
 * @param rdeps the reverse dependency array
 * @return status code
 **/
static inline int _free_rdeps(char** rdeps)
{
	uint32_t i;
	for(i = 0; rdeps[i] != NULL; i ++)
	    free(rdeps[i]);
	free(rdeps);
	return 0;
}
/**
 * @brief virtually remove a type from the type database, and remove all it's dependencies as well
 * @param sandbox the sandbox that we need to figure out the underlying operations
 * @param typename the typename for the type to remove
 * @return status code
 **/
static inline int _virtual_remove_type(sandbox_t* sandbox, const char* typename)
{
	/* We need to make sure the cache is already in the sandbox mode */
	proto_cache_sandbox_mode(1);

	int rc = proto_cache_full_type_name_exist(typename);
	if(ERROR_CODE(int) == rc) LOG_LIBPROTO_ERROR_RETURN(int);
	if(!rc) return 0;

	char const* const* rdeps_origin = proto_cache_revdep_get(typename, NULL);
	if(NULL == rdeps_origin) LOG_LIBPROTO_ERROR_RETURN(int);
	char** rdeps = _dup_rdeps(rdeps_origin);
	if(NULL == rdeps) ERROR_LOG_GOTO(ERR, "Cannot duplicate reverse depenecy list");

	if(ERROR_CODE(int) == proto_cache_delete(typename))
	    LOG_LIBPROTO_ERROR_GOTO(ERR);

	uint32_t i;
	for(i = 0; NULL != rdeps[i]; i ++)
	{
		int created = 0;
		_op_t* operation = _get_secondary_node(sandbox, rdeps[i], &created);
		if(NULL == operation)
		    ERROR_LOG_GOTO(ERR, "Cannot get the secondary operation node for type %s", rdeps[i]);

		/*
		if(operation->type_obj != NULL && ERROR_CODE(int) == proto_type_free(operation->type_obj))
		    ERROR_LOG_GOTO(ERR, "Cannot dispose the deleted type object");*/
		operation->removed = operation->type_obj;
		operation->type_obj = NULL;
		operation->is_update = 0;

		if(created && ERROR_CODE(int) == _virtual_remove_type(sandbox, rdeps[i]))
		    ERROR_LOG_GOTO(ERR, "Cannot remove the depenency for type %s", rdeps[i]);
	}

	return _free_rdeps(rdeps);
ERR:
	if(NULL != rdeps) _free_rdeps(rdeps);
	return ERROR_CODE(int);
}

/**
 * @brief do a virtual remove in a sandbox
 * @param sandbox the sandbox we want to test
 * @return status code
 **/
static inline int _virtual_removes(sandbox_t* sandbox)
{
	const _op_t* operation = sandbox->oplist;

	for(;operation != NULL; operation = operation->next)
	{
		if(operation->type_obj == NULL && ERROR_CODE(int) == _virtual_remove_type(sandbox, operation->target))
		    ERROR_RETURN_LOG(int, "Cannot virtually remove type %s", operation->target);
	}

	return 0;
}

/**
 * @brief virtually update the types
 * @param sandbox the sandbox we want to test
 * @return status code
 **/
static inline int _virtual_update_types(sandbox_t* sandbox)
{
	proto_cache_sandbox_mode(1);

	const _op_t* operation = sandbox->oplist;
	for(;operation != NULL; operation = operation->next)
	{
		if(operation->type_obj == NULL) continue;
		if(ERROR_CODE(int) == proto_cache_put(operation->target, operation->type_obj))
		    LOG_LIBPROTO_ERROR_RETURN(int);
	}

	return 0;
}

/**
 * @brief validate a single type in the sandbox, and if the type is broken, look at the sandbox flags
 *        to decide either we want to return an error or remove the type directly
 * @param sandbox the sandbox in test
 * @param typename the typename we want to validae
 * return status code
 **/
static inline int _validate_type(sandbox_t* sandbox, const char* typename)
{
	proto_cache_sandbox_mode(1);

	if(ERROR_CODE(int) == proto_db_type_validate(typename))
	{
		if(sandbox->flags == SANDBOX_FORCE_UPDATE)
		{
			if(ERROR_CODE(int) == _virtual_remove_type(sandbox, typename))
			    ERROR_RETURN_LOG(int, "Cannot remove the broken type");
			else proto_err_clear();

			int create;
			_op_t* op = _get_secondary_node(sandbox, typename, &create);
			if(NULL == op) ERROR_RETURN_LOG(int, "Cannot find the operation node");
			if(op->type_obj != NULL && ERROR_CODE(int) == proto_type_free(op->type_obj))
			    LOG_LIBPROTO_ERROR_RETURN(int);
			op->type_obj = NULL;
			/* because all it's reverse depenencies are removed, no need to validate anymore */
			return 0;
		}
		else
		    ERROR_RETURN_LOG(int, "Type %s will be broken", typename);
	}

	char const* const* rdeps_origin = proto_cache_revdep_get(typename, NULL);
	if(NULL == rdeps_origin) LOG_LIBPROTO_ERROR_RETURN(int);
	char** rdeps = _dup_rdeps(rdeps_origin);
	if(NULL == rdeps) ERROR_RETURN_LOG(int, "Cannot uplicate the reverse dependency array");

	uint32_t i;
	for(i = 0; NULL != rdeps[i]; i ++)
	{
		if(ERROR_CODE(int) == _validate_type(sandbox, rdeps[i]))
		{
			_free_rdeps(rdeps);
			return ERROR_CODE(int);
		}
	}
	_free_rdeps(rdeps);
	return 0;
}

/**
 * @brief finally we need to validate all the types in the sandbox, if the force deletion
 *        flag is given, then the broken type will be removed, rather than return a failure
 * @param sandbox the sandbox to validate
 * @return status code
 **/
static inline int _validate_sandbox_types(sandbox_t* sandbox)
{
	const _op_t* operation = sandbox->oplist;
	for(;operation != NULL; operation = operation->next)
	{
		if(operation->type_obj == NULL) continue;
		if(ERROR_CODE(int) == _validate_type(sandbox, operation->target))
		    return ERROR_CODE(int);
	}

	return 0;
}

/**
 * @brief fill one item to the pending operation buffer
 * @param op the operation object
 * @param buf the pending operation buffer
 * @param size the size of the buffer
 * @return status code
 * @note this function will always fill the element to the first element of the buffer
 **/
static inline uint32_t _fill_op_buf(const _op_t* op, sandbox_op_t* buf, size_t size)
{
	if(size == 0) return 0;

	if(op->type_obj != NULL)
	{
		if(op->is_update)
		{
			buf->target = op->target;
			buf->opcode = SANDBOX_UPDATE;
		}
		else
		{
			buf->target = op->target;
			buf->opcode = SANDBOX_CREATE;
		}
	}
	else
	{
		buf->target = op->target;
		buf->opcode = SANDBOX_DELETE;
	}

	return 1;
}

int sandbox_dry_run(sandbox_t* sandbox, sandbox_op_t* buf, size_t size)
{
	if(NULL == sandbox || NULL == buf)
	    ERROR_LOG_GOTO(ERR, "Invalid arguments");

	if(ERROR_CODE(int) == _virtual_removes(sandbox))
	    ERROR_LOG_GOTO(ERR, "Cannot validate all the type removal");

	if(ERROR_CODE(int) == _virtual_update_types(sandbox))
	    ERROR_LOG_GOTO(ERR, "Cannot virtually update the types");

	if(ERROR_CODE(int) == _validate_sandbox_types(sandbox))
	    ERROR_LOG_GOTO(ERR, "Cannot validate the result databaase");

	proto_cache_sandbox_mode(0);

	const _op_t* ptr = sandbox->oplist;
	for(;NULL != ptr; ptr = ptr->next)
	{
		if(ptr->type_obj != NULL && !proto_cache_full_type_name_exist(ptr->target))
		    ERROR_RETURN_LOG(int, "Type %s is not going to be installed", ptr->target);

		uint32_t written = _fill_op_buf(ptr, buf, size);
		size -= written;
		buf += written;
	}

	for(ptr = sandbox->secondary; NULL != ptr; ptr = ptr->next)
	{
		if(ptr->type_obj != NULL && !proto_cache_full_type_name_exist(ptr->target))
		    ERROR_RETURN_LOG(int, "Type %s is not going to be installed", ptr->target);

		uint32_t written = _fill_op_buf(ptr, buf, size);
		size -= written;
		buf += written;
	}

	if(size > 0) buf[0].opcode = SANDBOX_NOMORE;

	sandbox->validated = 1;
	return 0;
ERR:
	proto_cache_sandbox_mode(0);
	return ERROR_CODE(int);
}
/**
 * @brief Actually apply the operation list to the system
 * @param oplist the operation list
 * @return status code
 **/
static inline int _apply_list(_op_t* oplist)
{
	for(;NULL != oplist; oplist = oplist->next)
	{
		if(oplist->type_obj == NULL)
		{
			if(proto_cache_delete(oplist->target) == ERROR_CODE(int))
			    LOG_LIBPROTO_ERROR_RETURN(int);
		}
		else
		{
			if(proto_cache_put(oplist->target, oplist->type_obj) == ERROR_CODE(int))
			    LOG_LIBPROTO_ERROR_RETURN(int);
			else
			    oplist->type_obj = NULL;  /* Because the put function will take the owership at this time */
		}
	}

	return 0;
}

int sandbox_commit(sandbox_t* sandbox)
{
	if(NULL == sandbox)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(0 == sandbox->validated)
	    ERROR_RETURN_LOG(int, "Sandbox must be validated before commit");

	if(0 != sandbox->posted)
	    ERROR_RETURN_LOG(int, "Sandbox has already been posted to the system");

	if(ERROR_CODE(int) == _apply_list(sandbox->oplist))
	    ERROR_RETURN_LOG(int, "Cannot apply the operation list");

	if(ERROR_CODE(int) == _apply_list(sandbox->secondary))
	    ERROR_RETURN_LOG(int, "Cannot apply the secondary operation list");

	if(ERROR_CODE(int) == proto_cache_flush())
	    LOG_LIBPROTO_ERROR_RETURN(int);

	sandbox->posted = 1;

	return 0;
}
