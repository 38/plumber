/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the module definition for the Plumber Standard Service Module
 * @note this module is not actually a module that is able to manipulate some types of pipes
 *       Instead, this module provides the user-space program a set of lower-level function access
 * @brief module/pssm/module.h
 **/
#ifndef __PLUMBER_MODULE_PSSM_MODULE_H__
#define __PLUMBER_MODULE_PSSM_MODULE_H__

/**
 * @brief declare the external symbol for the module definition
 **/
extern itc_module_t module_pssm_module_def;

/**
 * @brief define the opcode we used
 **/
enum {
	MODULE_PSSM_MODULE_OPCODE_POOL_ALLOCATE,       /*!< the operation code used to allocate an object from the pool */
	MODULE_PSSM_MODULE_OPCODE_POOL_DEALLOCATE,     /*!< the operation code used to deallocate an object from the pool */
	MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_NEW,    /*!< the operation code used to create a new thread local */
	MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_GET,    /*!< the operation code used to get the pointer for current thread */
	MODULE_PSSM_MODULE_OPCODE_THREAD_LOCAL_FREE,   /*!< the operation code used to clean a used thread local up */
	MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_ADD,   /*!< add a new pointer to the request scope */
	MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_COPY,  /*!< copy a existing request scope pointer */
	MODULE_PSSM_MODULE_OPCODE_REQUEST_SCOPE_GET,   /*!< remove a existing request pointer */
	MODULE_PSSM_MODULE_OPCODE_ON_EXIT,             /*!< register an on exit callback, this will be used by the user-space library to do clean works */
	MODULE_PSSM_MODULE_OPCODE_PAGE_ALLOCATE,       /*!< allocate an entire page from the page memory pool */
	MODULE_PSSM_MODULE_OPCODE_PAGE_DEALLOCATE      /*!< deallocate the used page allocated from the page memory pool */
};

#endif /* __PLUMBER_MODULE_PSSM_MODULE_H__ */
