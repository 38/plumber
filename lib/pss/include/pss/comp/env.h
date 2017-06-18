/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The runtime environment managenment utilies for the code generation
 * @file pss/comp/env.h
 **/
#ifndef __PSS_COMP_ENV_H__
#define __PSS_COMP_ENV_H__

/**
 * @brief A runtime environment abstraction at compile time
 * @note This is actually the data structure we used to keep tracking the
 *       the register allocation and manages the scope, etc.
 **/
typedef struct _pss_comp_env_t pss_comp_env_t;

/**
 * @brief Create a new runtime environment abstraction
 * @return The newly created env
 **/
pss_comp_env_t* pss_comp_env_new();

/**
 * @brief Dispose a used env
 * @param env The env to dispose
 * @return status code
 **/
int pss_comp_env_free(pss_comp_env_t* env);

/**
 * @biref Open a new scope inside current scope
 * @note  This will allow the compile allocate variable valid thru we close the scope
 * @param env The abstract runtime envronment
 * @param new_closure indicates if this scope is actually an new closure <br/>
 *                    If this is true, which means if there's a local variable which
 *                    has the same named variable in the parent scope, we should be able
 *                    to use the same register. Because a closure scope means we have already
 *                    forked the register frame, and we do not need to worry about overriding
 *                    the variable will cause data lose because it's actually under different
 *                    data frame
 * @return status code
 **/
int pss_comp_env_open_scope(pss_comp_env_t* env, int new_closure);

/**
 * @brief Close current scope
 * @note  This will deallocate all the registers hold for the local variable inside this scope
 * @param env The abstract runtime environment
 * @return status code
 **/
int pss_comp_env_close_scope(pss_comp_env_t* env);

/**
 * @brief Get the register that associated with the given variable
 * @param env The abstract runtime environment
 * @param var The variable name
 * @param create Indicates if we should create a local variable when current scope doesn't have one  <br/>
 *               If this param is 0, it means we should look up the parent scope until we reach the global scope
 * @param regbuf The register buffer
 * @return The number of local register we found, 1 if we found a local varaiable, 0 is the sign for access the variable
 *         in globals, error code when there's anything wrong
 **/
int pss_comp_env_get_var(pss_comp_env_t* env, const char* var, int create, pss_bytecode_regid_t* regbuf);

/**
 * @brief Allocate an anonymous temporary register
 * @param env The abtract runtime environment
 * @return The register ID
 **/
pss_bytecode_regid_t pss_comp_env_mktmp(pss_comp_env_t* env);

/**
 * @brief Delete the anonymous temporary register
 * @param env The abtract runtime environment
 * @param tmp The tmp reigster
 * @return status code
 **/
int pss_comp_env_rmtmp(pss_comp_env_t* env, pss_bytecode_regid_t tmp);

#endif
