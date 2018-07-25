/**
 * Copyright (C) 2017-2018, Hao Hou
 **/
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <error.h>
#include <pservlet.h>
#include <pstd/mempool.h>
#include <pstd/scope.h>

static inline pipe_t _ensure_pipe(pipe_t current, const char* func)
{
	pipe_t ret = current;
	if(ERROR_CODE(pipe_t) == ret)
	{
		if(ERROR_CODE(pipe_t) == (ret = module_require_function("plumber.std", func)))
			ERROR_RETURN_LOG(pipe_t, "Cannot find servlet module function plumber.std.%s make sure you have installed pssm module", func);
	}

	return ret;
}

#define _ENSURE_PIPE(name, retval) \
    static pipe_t name = ERROR_CODE(pipe_t);\
    if(ERROR_CODE(pipe_t) == (name = _ensure_pipe(name, #name))) \
        return retval;

scope_token_t pstd_scope_add(const scope_entity_t* entity)
{
	_ENSURE_PIPE(scope_add, ERROR_CODE(scope_token_t));

	scope_token_t ret;

	if(ERROR_CODE(int) == pipe_cntl(scope_add, PIPE_CNTL_INVOKE, entity, &ret))
		ERROR_RETURN_LOG(scope_token_t, "Cannot finish the pipe_cntl call");

	return ret;
}

scope_token_t pstd_scope_copy(scope_token_t token, void** resbuf)
{
	_ENSURE_PIPE(scope_copy, ERROR_CODE(scope_token_t));

	scope_token_t ret;

	if(ERROR_CODE(int) == pipe_cntl(scope_copy, PIPE_CNTL_INVOKE, token, &ret, resbuf))
		ERROR_RETURN_LOG(scope_token_t, "Cannot finish the pipe_cntl call");

	return ret;
}

const void* pstd_scope_get(scope_token_t token)
{
	_ENSURE_PIPE(scope_get, NULL);

	const void* ret;

	if(ERROR_CODE(int) == pipe_cntl(scope_get, PIPE_CNTL_INVOKE, token, &ret))
		ERROR_PTR_RETURN_LOG("Cannot finish the pipe_cntl call");

	return ret;
}

pstd_scope_stream_t* pstd_scope_stream_open(scope_token_t token)
{
	_ENSURE_PIPE(scope_stream_open, NULL);

	void* ret = NULL;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_open, PIPE_CNTL_INVOKE, token, &ret))
		ERROR_PTR_RETURN_LOG("Cannot finish the pipe_cntl call");

	return (pstd_scope_stream_t*)ret;
}

size_t pstd_scope_stream_read(pstd_scope_stream_t* stream, void* buf, size_t size)
{
	_ENSURE_PIPE(scope_stream_read, ERROR_CODE(size_t));

	size_t ret = 0;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_read, PIPE_CNTL_INVOKE, stream, buf, size, &ret))
		ERROR_RETURN_LOG(size_t, "Cannot finish the pipe_cntl call");

	return ret;
}

int pstd_scope_stream_eof(const pstd_scope_stream_t* stream)
{
	_ENSURE_PIPE(scope_stream_eof, ERROR_CODE(int));

	int ret = 0;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_eof, PIPE_CNTL_INVOKE, stream, &ret))
		ERROR_RETURN_LOG(int, "Cannot finish the pipe_cntl call");

	return ret;
}

int pstd_scope_stream_close(pstd_scope_stream_t* stream)
{
	_ENSURE_PIPE(scope_stream_close, ERROR_CODE(int));

	return pipe_cntl(scope_stream_close, PIPE_CNTL_INVOKE, stream);
}

int pstd_scope_stream_ready_event(pstd_scope_stream_t* stream, scope_ready_event_t* buf)
{
	_ENSURE_PIPE(scope_stream_ready_event, ERROR_CODE(int));

	int ret = 0;

	if(ERROR_CODE(int) == pipe_cntl(scope_stream_ready_event, PIPE_CNTL_INVOKE, stream, buf, &ret))
		ERROR_RETURN_LOG(int, "Cannot finish the pipe_cntl call");

	return ret;
}

#define _GC_MAGIC 0x3361fea8f61fea8full

/**
 * @brief The actual RLS object managed by GC
 **/
typedef struct {
	uint64_t                 magic;      /*!< The magic number to verify this is a GC object */
	pstd_scope_gc_obj_t      gc_obj[0];  /*!< The address for a gc object exposed to exteranl */
	scope_entity_t           ent;        /*!< The actual inner RLS entity */
	uint32_t                 refcnt;     /*!< The reference counter */
} _gc_object_t;
STATIC_ASSERTION_OFFSET_EQ_ID(_GC_OBJECT_OBJ, _gc_object_t, gc_obj[0].obj, _gc_object_t, ent.data);

/**
 * @brief Check if the gc_obj is well-formed
 * @param gc_obj The gc object
 * @return The GC object or NULL if check failed
 **/
static inline _gc_object_t* _gc_object_check(pstd_scope_gc_obj_t* gc_obj)
{
	uintptr_t addr = (uintptr_t)gc_obj - offsetof(_gc_object_t, gc_obj);
	_gc_object_t* obj = (_gc_object_t*)addr;

	if(obj->magic != _GC_MAGIC)
		ERROR_PTR_RETURN_LOG("Invaid object");

	return obj;
}

/**
 * @brief Dispose the GC wrapper
 * @note This function is called by the framework when the scope is dead.
 *       So what we should do is check if the inner object has dead alread,
 *       if so, we just need to return the memory for this wrapper
 **/
static int _gc_free(void* ptr)
{
	_gc_object_t* gc = (_gc_object_t*)ptr;

	/* If we have data to dispose and the free function is not empty
	 * Just call the free function before we dispose the wrapper itself */
	if(gc->ent.data != NULL && gc->ent.free_func != NULL &&
	   ERROR_CODE(int) == gc->ent.free_func(gc->ent.data))
		ERROR_RETURN_LOG(int, "Cannot dispose the object");

	return pstd_mempool_free(ptr);
}

/**
 * @brief Once we copy a new GC wrapped object,
 *        we need to copy the inner object as well as the wrapper
 **/
static void* _gc_copy(const void* ptr)
{
	const _gc_object_t* gc = (const _gc_object_t*)ptr;

	/* Check if we can copy it */
	if(gc->ent.data == NULL || gc->ent.copy_func == NULL)
		ERROR_PTR_RETURN_LOG("Copy is impossible");

	/* Allocate new GC wrapper for the result */
	_gc_object_t* obj = (_gc_object_t*)pstd_mempool_alloc(sizeof(_gc_object_t));

	if(NULL == obj)
		ERROR_PTR_RETURN_LOG("Allocation failure");

	/* Ok, let's copy the data */
	memcpy(obj, gc, sizeof(_gc_object_t));

	/* Then copy the inner object */
	if(NULL == (obj->ent.data = obj->ent.copy_func(gc->ent.data)))
		ERROR_LOG_GOTO(ERR, "Cannot copy the inner data object");

	/* Nothing has reference to it */
	obj->refcnt = 0;

	return obj;
ERR:
	pstd_mempool_free(obj);
	return NULL;
}

static void* _gc_open(const void* ptr)
{
	const _gc_object_t* gc = (const _gc_object_t*)ptr;

	if(gc->ent.data == NULL || gc->ent.open_func == NULL)
		ERROR_PTR_RETURN_LOG("Copy is impossible");

	return gc->ent.open_func(gc->ent.data);
}

scope_token_t pstd_scope_gc_add(const scope_entity_t* entity, pstd_scope_gc_obj_t ** objbuf)
{
	if(NULL == entity)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	_gc_object_t* obj = (_gc_object_t*)pstd_mempool_alloc(sizeof(_gc_object_t));

	if(NULL == obj)
		ERROR_RETURN_LOG(scope_token_t, "Allocation failure");

	obj->magic = _GC_MAGIC;
	obj->refcnt = 0;

	memcpy(&obj->ent, entity, sizeof(scope_entity_t));

	scope_entity_t gc_ent = {
		.data      = obj,
		.copy_func = _gc_copy,
		.free_func = _gc_free,
		.open_func = _gc_open,
		.read_func = entity->read_func,
		.eos_func  = entity->eos_func,
		.event_func = entity->event_func,
		.close_func = entity->close_func
	};

	if(NULL != objbuf)
		*objbuf = obj->gc_obj;

	return pstd_scope_add(&gc_ent);
}

pstd_scope_gc_obj_t* pstd_scope_gc_get(scope_token_t token)
{
	const _gc_object_t* gc = (const _gc_object_t*)pstd_scope_get(token);

	if(NULL == gc) return NULL;

	union {
		const pstd_scope_gc_obj_t*  cst;
		pstd_scope_gc_obj_t*        mut;
	} ret = { .cst = gc->gc_obj };

	return ret.mut;
}

int pstd_scope_gc_incref(pstd_scope_gc_obj_t* gc_obj)
{
	if(NULL == gc_obj)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_gc_object_t* obj;

	if(NULL == (obj = _gc_object_check(gc_obj)))
		ERROR_RETURN_LOG(int, "Invalid GC object");

	if(obj->ent.data == NULL)
		ERROR_RETURN_LOG(int, "incref is impossible");

	obj->refcnt ++;

	return 0;
}

int pstd_scope_gc_decref(pstd_scope_gc_obj_t* gc_obj)
{
	if(NULL == gc_obj)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_gc_object_t* obj;

	if(NULL == (obj = _gc_object_check(gc_obj)))
		ERROR_RETURN_LOG(int, "Invalid GC object");

	if(obj->ent.data == NULL)
		ERROR_RETURN_LOG(int, "decref is impossible");

	if(obj->refcnt > 0) obj->refcnt --;

	if(0 == obj->refcnt)
	{
		if(NULL != obj->ent.free_func && ERROR_CODE(int) == obj->ent.free_func(obj->ent.data))
			ERROR_RETURN_LOG(int, "Cannot dispose the actual data object");

		obj->ent.data = NULL;
	}

	return 0;
}
