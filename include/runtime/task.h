/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file runtime/task.h
 * @brief The data strcture that used to describe a task. A task is the abstract of one
 *        execution of the servlet
 **/

#ifndef __PLUMBER_RUNTIME_TASK_H__
#define __PLUMBER_RUNTIME_TASK_H__

/** @brief the mask used for the action bit */
#define RUNTIME_TASK_FLAG_ACTION_MASK 0xc0000000u

/** @brief Indicates that this task is an init task */
#define RUNTIME_TASK_FLAG_ACTION_INIT 0x00000000u

/** @brief indicates that this task is an execute task */
#define RUNTIME_TASK_FLAG_ACTION_EXEC 0x40000000u

/** @brief indicate that this task is a finalization task */
#define RUNTIME_TASK_FLAG_ACTION_UNLOAD 0x80000000u

/** @brief the flag that indicate that this task has been invoked */
#define RUNTIME_TASK_FLAG_ACTION_INVOKED 0x20000000u

/** @brief The flag indicates this is an async task */
#define RUNTIME_TASK_FLAG_ACTION_ASYNC   0x10000000u

/** @brief the type for the task flags */
typedef uint32_t runtime_task_flags_t;
/**
 * @brief get the action type
 * @param flag the PIPE flag
 **/
#define RUNTIME_TASK_FLAG_GET_ACTION(flag) ((flag)&RUNTIME_TASK_FLAG_ACTION_MASK)

/**
 * @brief the struct used to describe a task  <br/> the task is a execution instance of a servlet
 **/
typedef struct {
	runtime_api_task_id_t id;            /*!< The task ID */
	runtime_task_flags_t  flags;         /*!< The flag of this task */
	runtime_servlet_t*    servlet;       /*!< The servlet has been activated in this task */
	size_t                npipes;        /*!< The number of pipes for this task */
	uint32_t              async_owner:1; /*!< This indicates this task actually owns this async buffer, which means it's this task's responsibility to 
										  *   dispose the async data buffer. When a async_setup task has been created, it actually holds the buffer ownership
										  *   for a while, util all it's companions have been created, since then, the async_cleanup task will hold the ownership
										  *   This means, if we don't have the async_cleanup task created, we need to dispose the async buffer for sure.
										  *   At this point, we introduced an assumption, once the async_cleanup task is created, it must be disposed later,
										  *   but this is obviously true, otherwise we should have memory leak issue */
	void*                 async_data;    /*!< The async task data buffer */
	itc_module_pipe_t*    pipes[0];      /*!< The pipe table for this task */
} runtime_task_t;

STATIC_ASSERTION_LAST(runtime_task_t, pipes);
STATIC_ASSERTION_SIZE(runtime_task_t, pipes, 0);

/**
 * @brief initialization of this file
 * @return <0 when error
 **/
int runtime_task_init();

/**
 * @brief finalization of this file
 * @return <0 when error
 **/
int runtime_task_finalize();

/**
 * @brief free a used task
 * @param task the target task
 * @return < 0 indicates an error
 **/
int runtime_task_free(runtime_task_t* task);

/**
 * @brief create a new task
 * @note If the servlet is an async servlet, and flag is EXEC, we are creating a async_init task
 * @todo implement the note
 * @param servlet the target servlet
 * @param flags the flags of this task
 * @return the newly created task
 **/
runtime_task_t* runtime_task_new(runtime_servlet_t* servlet, runtime_task_flags_t flags);

/**
 * @brief Check if this task is an async task
 * @param task The task we want to check
 * @todo implement this
 * @return If the task is the async task or error code
 **/
int runtime_task_is_async(const runtime_task_t* task);

/**
 * @brief This function will create the companions of the asnyc task
 * @param task The async_exec task
 * @param exec_buf The buffer used to return the execz_buf
 * @param cleanup_buf The buffer used to return cleanup_buf
 * @note  This function will copy the task pipe table from the given task, which means uless the
 *        scheduler is sure all the pipe has been assigned all the pipes to the task, this function shouldn't be called.
 *        By which means, this function should be called just before the async_init function is to be executed.
 * @todo implement this
 * @return status code
 **/
int runtime_task_async_companions(runtime_task_t* task, runtime_task_t** exec_buf, runtime_task_t** cleanup_buf);

/**
 * @brief start the task
 * @param task the task to start
 * @param data an additional data would pass to the task, this will only used for the async_setup and async_exec tasks
 * @note  change this
 * @return the status code
 **/
int runtime_task_start(runtime_task_t* task, runtime_api_async_handle_t* handle);

/**
 * @brief This is the fast version to start an exec task
 * @details This function is dangerous, because it won't do param check and will assume
 *          The task is exec task. In addition, it do not recover the current task variable
 *          This function is completely for performance purpose, because most of the task
 *          are exec task and we should have a way to do it faster
 * @todo Please becareful, now we could have async tasks being started, so make sure this is not
 *       an async task, and then we are able to move on
 * @param task The task to start
 * @return status code
 **/
int runtime_task_start_exec_fast(runtime_task_t* task);

/**
 * @brief This is the similar function for the runtime_task_start_async_init_fast
 * @details In this function we will assume we have a valid async_setup task, otherwise
 *          it's extermely dangerous
 * @todo determine where we should use this function
 * @param task The task to start
 * @param async_handle The pointer to the async handle
 * @return status code
 **/
int runtime_task_start_async_setup_fast(runtime_task_t* task, runtime_api_async_handle_t* async_handle);

/**
 * @brief This is the faster version function to start a async_cleanup task
 * @details This function will assume the caller guareentee everything, so it's dangerous
 *          see the documentation for runtime_task_start_async_setup_fast and runtime_task_start_exec_fast
 *          for more details
 * @param task The task to start
 * @return status code
 **/
int runtime_task_start_async_cleanup_fast(runtime_task_t* task);

/**
 * @brief get current task
 * @return the task object of current task, NULL if there's an error
 **/
runtime_task_t* runtime_task_current();

#endif /* __PLUMBER_RUNTIME_TASK_H__ */
