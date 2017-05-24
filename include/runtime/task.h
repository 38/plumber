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
	runtime_api_task_id_t id;   /*!< The task ID */
	runtime_task_flags_t flags; /*!< The flag of this task */
	runtime_servlet_t* servlet; /*!< The servlet has been activated in this task */
	size_t npipes;              /*!< The number of pipes for this task */
	itc_module_pipe_t* pipes[0];/*!< The pipe table for this task */
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
 * @param servlet the target servlet
 * @param flags the flags of this task
 * @return the newly created task
 **/
runtime_task_t* runtime_task_new(runtime_servlet_t* servlet, runtime_task_flags_t flags);

/**
 * @brief start the task
 * @param task the task to start
 * @return the status code
 **/
int runtime_task_start(runtime_task_t* task);

/**
 * @brief This is the fast version to start an exec task
 * @details This function is dangerous, because it won't do param check and will assume
 *          The task is exec task. In addition, it do not recover the current task variable
 *          This function is completely for performance purpose, because most of the task
 *          are exec task and we should have a way to do it faster
 * @param task The task to start
 * @return status code
 **/
int runtime_task_start_exec_fast(runtime_task_t* task);

/**
 * @brief get current task
 * @return the task object of current task, NULL if there's an error
 **/
runtime_task_t* runtime_task_current();

#endif /* __PLUMBER_RUNTIME_TASK_H__ */
