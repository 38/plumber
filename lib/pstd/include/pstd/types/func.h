/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief  Wrapper a function into a RLS object
 * @file   pstd/include/types/func.h
 **/
#ifndef __PSTD_FUNC_H__
#define __PSTD_FUNC_H__

/**
 * @brief The function object 
 **/
typedef struct _pstd_func_t pstd_func_t;

/**
 * @brief The actual function to run
 * @param env The environment or closure of this function object
 * @param buf The result buffer
 * @param func_args The function arguments
 * @return The status code of the invocation
 **/
typedef int (*pstd_func_code_t)(const void* env, void* __restrict buf, va_list func_args);

/**
 * @brief The callback function that is used to dispose a function object
 * @return The status code
 **/
typedef int (*pstd_func_free_t)(void* env);

/**
 * @brief Create a new function object
 * @param trait The type of this function
 * @param env The environment variable of the object
 * @param code The code to run (NULL for the function that is used for code generation)
 * @param free The function that will be called to free the environment
 * @note The ownership of the env should be transferred, otherwise env should be disposed by caller
 * @return The newly created function object
 **/
pstd_func_t* pstd_func_new(uint64_t trait, void* env, pstd_func_code_t code, pstd_func_free_t free);

/**
 * @brief Dispose a used function object
 * @param func The function object
 * @return status code
 **/
int pstd_func_free(pstd_func_t* func);

/**
 * @brief Commit a function object to RLS
 * @param func The function boject to commit
 * @param gc   If we should use reference counter to collect the object
 * @return Token or error code
 **/
scope_token_t pstd_func_commit(pstd_func_t* func, uint32_t gc);

/**
 * @brief Get a function object from the RLS
 * @param token The token we want to get from the RLS
 * @param gc If the object is managed by gc
 * @return The pointer  to the function o bject 
 **/ 
const pstd_func_t* pstd_func_from_rls(scope_token_t token, uint32_t gc);

/**
 * @brief Get the type code of the functoin object
 * @param func The function
 * @return The function code or error code
 **/
uint64_t pstd_func_get_trait(const pstd_func_t* func);

/**
 * @brief Call the function and put the result to the reulst buffer
 * @param func The function object
 * @param result The result buffer
 * @return status code
 **/
int pstd_func_invoke(const pstd_func_t* func, void* result, ...);

/**
 * @brief Get the GC object (if the function is managed by the GC)
 * @param func The function to get
 * @note If the function is not managed by GC, return NULL
 * @return the GC object
 **/
pstd_scope_gc_obj_t* pstd_func_get_gc_obj(const pstd_func_t* func);

/**
 * @brief Get the environment variables for this function object
 * @param func The function object
 * @return The environment object
 **/
void* pstd_func_get_env(const pstd_func_t* func);

#endif /* __PSTD_FUNC_H__ */
