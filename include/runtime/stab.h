/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file stab.h
 * @brief The define of the servlet table, the data structure that manage all the servlets
 * @details we manipulate a servlet table and all the external file calls servlet by sid rather than the actuall servlet
 **/
#ifndef __PLUMBER_RUNTIME_STAB_H__
#define __PLUMBER_RUNTIME_STAB_H__

/**
 * @brief the data type for a servlet table entry
 **/
typedef uint32_t runtime_stab_entry_t;

/** @brief init the servlet module
 *  @return < 0 on error
 **/
int runtime_stab_init();

/** @brief release the memory used by this part
 *  @return < 0 on error
 **/
int runtime_stab_finalize();

/**
 * @brief set the owner of the servlet instance
 * @param sid the servlet id to set
 * @param owner the owner back reference
 * @param reuse_servlet the flag indicates if we need reuse the servlet
 * @note  In fact, we don't want to reuse any servlet, however, there's another
 *        use case in testing we want to reuse them. DO NOT pass the reuse flag
 *        unless you know what you are doing
 * @note  Once the owner back reference is set up, we do not allow them be removed
 * @todo  There may be some issue when we allow hot deploy, then the stab may need to be cleanup,
 *        and then we may need remove some entry that is no longer used.
 * @return status code
 **/
int runtime_stab_set_owner(runtime_stab_entry_t sid, const void* owner, int reuse_servlet);

/**
 * @brief Get the owner of the servlet instance
 * @param sid the servlet id to get
 * @return the owner, NULL if there's no such onwer
 * @note we consider this function have no error case
 **/
const void* runtime_stab_get_owner(runtime_stab_entry_t sid);

/** @brief load a servlet instance by the given condition
 *  @param argc the number of argument
 *  @param argv the argument list
 *	@return the servlet id, <0 when error
 **/
runtime_stab_entry_t runtime_stab_load(uint32_t argc, char const * const * argv);


/** @brief Create a task that is to run a servlet
 *  @param sid the servlet id
 *  @param flags the task flags
 *  @note the param is not decided, just a place holder
 *  @return the servelet, NULL for error cases
 **/
runtime_task_t* runtime_stab_create_exec_task(runtime_stab_entry_t sid, runtime_task_flags_t flags);

/**
 * @brief query how many pipe is going to be use d by this servlet
 * @param sid the servlet id to query
 * @return the number of pipe that is used by this servlet
 **/
size_t runtime_stab_num_pipes(runtime_stab_entry_t sid);

/**
 * @brief query the pipe ID by the pipe name and the servlet ID
 * @param sid the servlet id to query
 * @param pipe the pipe name to query
 * @return the pipe id or a negative error code
 **/
runtime_api_pipe_id_t runtime_stab_get_pipe(runtime_stab_entry_t sid, const char* pipe);

/**
 * @brief query the pipe flags by the pipe ID and the servlet ID
 * @param sid the servlet ID to query
 * @param pipe the pipe id to query
 * @return the pipe flag or a negative error code
 **/
runtime_api_pipe_flags_t runtime_stab_get_pipe_flags(runtime_stab_entry_t sid, runtime_api_pipe_id_t pipe);

/**
 * @brief query how many input pipes are required by this servlet
 * @param sid the servlet ID
 * @return the number of the input or a negative error code
 **/
int runtime_stab_get_num_input_pipe(runtime_stab_entry_t sid);

/**
 * @brief query how many output pipes are supported by this servlet
 * @param sid the servlet ID
 * @return the number of the output or a neative error code
 **/
int runtime_stab_get_num_output_pipe(runtime_stab_entry_t sid);

/**
 * @brief get the pipe description table of the servlet
 * @param sid the servlet id
 * @return the PDT of this servlet
 **/
const runtime_pdt_t* runtime_stab_get_pdt(runtime_stab_entry_t sid);

/**
 * @brief get the description of the servlet
 * @param sid the servlet id
 * @return the description or NULL on error cases
 **/
const char* runtime_stab_get_description(runtime_stab_entry_t sid);

/**
 * @brief get the version number of the servlet
 * @param sid the servlet id
 * @return the servlet version number, (uint32_t)-1 on error cases
 **/
uint32_t runtime_stab_get_version(runtime_stab_entry_t sid);

/**
 * @brief get the initialization arguments of the servlet
 * @param sid the target servlet id
 * @param argc the buffer that returns the number of args
 * @return NULL on error, otherwise the pointer to the arguments
 **/
char const* const* runtime_stab_get_init_arg(runtime_stab_entry_t sid, uint32_t* argc);

/**
 * @brief dispose all the servlet intances in the stab
 * @return status code
 **/
int runtime_stab_dispose_instances();

#endif /* __PLUMBER_RUNTIME_SERVLET_TAB_H__ */
