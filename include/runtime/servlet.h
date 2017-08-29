/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file servlet.h
 * @brief the servlet defination
 * @details servlet is a concept which is a program smaller than a traditional service,
 *           the servlet can be connected by pipe infrastructure provided by the framework
 **/
#include <stdint.h>
#include <constants.h>
#include <utils/static_assertion.h>
#include <utils/mempool/objpool.h>

#ifndef __PLUMBER_RUNTIME_SERVLET_H__
#define __PLUMBER_RUNTIME_SERVLET_H__
/**
 * @brief the binary interface for a servlet
 **/
typedef struct {
	runtime_api_servlet_def_t* define;                         /*!< The additional data that used by the servlet */
	void*                      dl_handler;                     /*!< The dynamic library handler */
	char                       name[RUNTIME_SERVLET_NAME_LEN]; /*!< The name of the servlet */
	mempool_objpool_t*         async_pool;                     /*!< The memory pool for the async buffer for this servlet, it's only meaning ful if this servlet is async */
} runtime_servlet_binary_t;

/**
 * @brief The data structure for a initialized servelet
 * @note  For each servlet instance, we only allow use them in the service graph once. <br/>
 *        Because for some generic servlet, even the arguments are the same, the type and
 *        the behavior may be different because of the context of the servlet.
 *        Thus, we do not reuse the servlet instance among different nodes even tough they are
 *        using the same initialization arguments. <br/>
 *        In addition, if we share the context, the context is not isolated per node. <br/>
 **/
typedef struct{
	runtime_servlet_binary_t*       bin;        /*!< The binary interface */
	uint32_t                        async:1;    /*!< If this is an async servlet */
	uint32_t                        argc;       /*!< The number of argument has been pass to this servlet */
	char**                          argv;       /*!< The argument list for this servlet*/
	runtime_pdt_t*                  pdt;        /*!< The pipe name table */
	mempool_objpool_t*              task_pool;  /*!< The memory pool for the task created from this servlet */
	const void*                     owner;      /*!< The pointer used to make a back reference to the service node owns this servlet */
	runtime_api_pipe_t              sig_null;   /*!< The pipe used as the zero output signal */
	runtime_api_pipe_t              sig_error;  /*!< The pipe used as the internal error signal */
	uintptr_t __padding__[0];
	char                            data[0];    /*!< The additional global memory space for this servlet */
} runtime_servlet_t;
/* Memory layout assertions */
STATIC_ASSERTION_LAST(runtime_servlet_t, data);
STATIC_ASSERTION_SIZE(runtime_servlet_t, data, 0);

/**
 * @brief a trap function
 * @param the trap ID
 **/
typedef void (*runtime_servlet_trap_func_t)(int trap_id);

/**
 * @brief initlaize this file
 * @return status code
 **/
int runtime_servlet_init();

/**
 * @brief finalize this file
 * @return status code
 **/
int runtime_servlet_finalize();

/**
 * @brief append the search path for a servlet
 * @param path a list of path (ends with NULL)
 * @return < 0 when error
 **/
int runtime_servlet_append_search_path(const char* path);

/**
 * @brief clear the search path
 * @return < 0 when error
 **/
int runtime_servlet_clear_search_path();

/**
 * @brief get the number of search paths
 * @return the search path, < 0 when error
 **/
size_t runtime_servlet_num_search_path();

/**
 * @brief get the search path array
 * @return the pointer to the search path array, NULL when error
 **/
const char * const * runtime_servlet_search_paths();

/**
 * @brief find a matched binary in the search path
 * @param servlet the servlet name
 * @return the full path to the servlet binary, NULL when error
 **/
const char* runtime_servlet_find_binary(const char* servlet);

/**
 * @brief load servlet from given binary
 * @param path the path to the servlet
 * @param name the name of the servlet (name is a identifier that can be used in the service description script)
 * @return the pointer to the loaded servlet, NULL on error
 **/
runtime_servlet_binary_t* runtime_servlet_binary_load(const char* path, const char* name);


/**
 * @brief unload a binary from memory
 * @param binary the servlet to unload
 * @return < 0 on error
 **/
int runtime_servlet_binary_unload(runtime_servlet_binary_t* binary);

/**
 * @brief initialize the servlet with given argument
 * @param binary the servlet binary to initialize
 * @param argc the number of arguments
 * @param argv the value arrray for arguments
 * @return the newly initialized servlet, NULL on error
 **/
runtime_servlet_t* runtime_servlet_new(runtime_servlet_binary_t* binary, uint32_t argc, char const* const* argv);

/**
 * @brief free the servlet instance, but do not free the binary object
 * @param servlet the target servlet
 * @return the status code
 **/
int runtime_servlet_free(runtime_servlet_t* servlet);

/**
 * @brief set a trap used for debug or test
 * @param func a function pointer to the trap function. If NULL has been passed, clear the existing trap
 * @return error code, < 0 indicates error
 **/
int runtime_servlet_set_trap(runtime_servlet_trap_func_t func);

#endif /* __PLUMBER_RUNTIME_SERVLET_H__ */
