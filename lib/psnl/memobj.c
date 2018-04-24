/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <psnl/memobj.h>

/**
 * @brief The actual data struture for the PSNL memory object
 **/
struct _psnl_memobj_t {
	void*                           obj;            /*!< The pointer to the actual data */
	uint32_t                        refcnt;         /*!< The reference counter */
	uint32_t                        committed:1;    /*!< Indicates if the memory object has been committed */
	uint64_t                        magic;          /*!< The magic number for this memory object */
	psnl_memobj_dispose_func_t      dispose;        /*!< The how to dispose the used memory object, if missing, use free */
	void*                           cb_user_data;   /*!< The user data used by the callback functions */
};

/**
 * @brief Get the pointer that has the write access
 * @note Although this is not common to have this kinds of operation, but the fact that the reference counter
 *       is actually something that needs to be modified even we only have a read-only pointer. 
 *       Because even the read-only pointer can have the need to hold the object from being disposed.
 *       The better solution could be seperate the refcnt with the majority part of the data.
 *       But the struct is only acceptable within this file, and it's actually reasonable we just do the modification
 * @return the writable pointer
 **/
static inline psnl_memobj_t* _get_write_pointer(const psnl_memobj_t* memobj)
{
	union {
		const psnl_memobj_t* from;
		psnl_memobj_t*       to;
	} convert = { .from = memobj };

	return convert.to;
}

/**
 * @brief Dispose the actual object
 * @param obj The memory object wrapper
 * @return stauts code
 **/
static inline int _dispose_inner_object(psnl_memobj_t* obj)
{
	if(NULL == obj) return 0;

	if(NULL != obj->dispose)
	{
		if(ERROR_CODE(int) == obj->dispose(obj->obj, obj->cb_user_data))
			ERROR_RETURN_LOG(int, "Cannot dispose the object");
	}
	else free(obj->obj);

	obj->obj = NULL;

	return 0;
}

psnl_memobj_t* psnl_memobj_new(psnl_memobj_param_t param, const void* create_data)
{
	if(NULL == param.obj && NULL == param.create_cb)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	psnl_memobj_t* ret = pstd_mempool_alloc(sizeof(psnl_memobj_t));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot create the memory object wrapper");

	memset(ret, 0, sizeof(psnl_memobj_t));

	ret->magic = param.magic;
	
	if(param.obj == NULL && NULL == (ret->obj = param.create_cb(create_data)))
		ERROR_LOG_GOTO(ERR, "Cannot create the memory object");

	ret->refcnt = 0;

	ret->dispose = param.dispose_cb;
	ret->cb_user_data = param.dispose_cb_data;

	return ret;
ERR:
	pstd_mempool_free(ret);
	return NULL;
}

int psnl_memobj_free(psnl_memobj_t* obj)
{
	if(NULL == obj)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;

	if(NULL != obj->obj && ERROR_CODE(int) == _dispose_inner_object(obj))
		rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == pstd_mempool_free(obj))
		rc = ERROR_CODE(int);

	return rc;
}

int psnl_memobj_incref(const psnl_memobj_t* memobj)
{
	if(NULL == memobj)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL == memobj->obj)
		ERROR_RETURN_LOG(int, "Cannot incref a disposed memory object wrapper");

	psnl_memobj_t* wr = _get_write_pointer(memobj);

	/* Actually all the token needs to be incref'ed and decref'ed from the same worker thread */
	wr->refcnt ++;

	return 0;
}

int psnl_memobj_decref(const psnl_memobj_t* memobj)
{
	if(NULL == memobj)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL == memobj->obj)
		ERROR_RETURN_LOG(int, "Cannot decref a dispose memory object wrapper");

	psnl_memobj_t* wr = _get_write_pointer(memobj);

	if(wr->refcnt > 0) wr->refcnt --;

	if(wr->refcnt == 0 && ERROR_CODE(int) == _dispose_inner_object(wr))
		ERROR_RETURN_LOG(int, "Cannot dispose the inner object");

	return 0;
}

const void* psnl_memobj_get_const(const psnl_memobj_t* memobj, uint64_t magic)
{
	if(NULL == memobj)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(memobj->magic != magic)
		ERROR_PTR_RETURN_LOG("Unexpected object magic number");

	return memobj->obj;
}

void* psnl_memobj_get(psnl_memobj_t* memobj, uint64_t magic)
{
	if(NULL == memobj)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(memobj->magic != magic)
		ERROR_PTR_RETURN_LOG("Unexpected object magic number");

	return memobj->obj;
}

int psnl_memobj_set_committed(psnl_memobj_t* memobj, int val)
{
	if(NULL == memobj)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	memobj->committed = (val != 0);

	return 0;
}

int psnl_memobj_is_committed(const psnl_memobj_t* memobj)
{
	if(NULL == memobj)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	return memobj->committed != 0;
}
