/**
 * Coypright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>

#include <pstd/mempool.h>
#include <pstd/scope.h>
#include <pstd/types/func.h>

struct _pstd_func_t {
	uint64_t             trait;   /*!< The trait id, which indicates what kinds of interface the function object should use  */
	pstd_scope_gc_obj_t* gc_obj;  /*!< The gc object, if this object is not managed by GC, this should be NULL */
	pstd_func_code_t     code;    /*!< The actual code to run */
	pstd_func_free_t     free;    /*!< The free callback */
	void*                env;     /*!< The environment */
	uint32_t             committed:1; /*!< If the function has been committed */
};

pstd_func_t* pstd_func_new(uint64_t trait, void* env, pstd_func_code_t code, pstd_func_free_t free)
{
	if(ERROR_CODE(uint64_t) == trait)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	pstd_func_t* ret = pstd_mempool_alloc(sizeof(pstd_func_t));

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot create new function object");

	ret->committed = 0;
	ret->trait = trait;
	ret->code = code;
	ret->free = free;
	ret->env = env;

	ret->gc_obj = NULL;

	return ret;
}

int pstd_func_free(pstd_func_t* func)
{
	if(NULL == func)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(func->committed)
	    ERROR_RETURN_LOG(int, "Disposing a committed RLS object is not allowed");

	if(NULL == func->free) return 0;

	return func->free(func->env);
}

static int _func_free(void* obj)
{
	pstd_func_t* func = (pstd_func_t*)obj;

	if(NULL != func->free && ERROR_CODE(int) == func->free(func->env))
	    ERROR_RETURN_LOG(int, "Cannot dispose the function environment");

	return pstd_mempool_free(func);
}


scope_token_t pstd_func_commit(pstd_func_t* func, uint32_t gc)
{
	if(NULL == func || func->committed)
	    ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	scope_entity_t ent = {
		.data = func,
		.free_func = _func_free
	};

	if(gc)
	    return pstd_scope_gc_add(&ent, &func->gc_obj);

	return pstd_scope_add(&ent);
}

const pstd_func_t* pstd_func_from_rls(scope_token_t token, uint32_t gc)
{
	if(ERROR_CODE(scope_token_t) == token)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(gc)
	{
		pstd_scope_gc_obj_t* gc_obj = pstd_scope_gc_get(token);
		if(NULL == gc_obj)
		    ERROR_PTR_RETURN_LOG("Cannot get the GC object");

		return (const pstd_func_t*)gc_obj->obj;
	}

	return (const pstd_func_t*)pstd_scope_get(token);
}

uint64_t pstd_func_get_trait(const pstd_func_t* func)
{
	if(NULL == func)
	    ERROR_RETURN_LOG(uint64_t, "Invalid arguments");

	return func->trait;
}

int pstd_func_invoke(const pstd_func_t* func, void* result, ...)
{
	if(NULL == func)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;

	if(NULL != func->code)
	{
		va_list ap;
		va_start(ap, result);

		rc = func->code(func->env, result, ap);

		va_end(ap);
	}

	return rc;
}

pstd_scope_gc_obj_t* pstd_func_get_gc_obj(const pstd_func_t* func)
{
	return func == NULL ? NULL : func->gc_obj;
}

void* pstd_func_get_env(const pstd_func_t* func)
{
	return func == NULL ? NULL : func->env;
}
