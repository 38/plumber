/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The async task support
 * @details The async task is the task that do not actually 
 *          occupies the worker thread. Instead, it runs with the
 *          async task thread pool, and when the task is done the 
 *          async task thread will emit a task event which will be able to 
 *          wake the pending task up in the scheduler task. <br/>
 *          This mechamism is useful, if we need to performe some slow operation in
 *          a servlet. Because this types of task won't block the worker thread
 **/
#ifndef __SCHED_ASYNC_H__
#define __SCHED_ASYNC_H__

typedef struct {
	void*                          data;         /*!< The data used by this task */
	void*                          thread_data;  /*!< The source task scheduler thread context */
	void*                          sched_data;   /*!< The additional scheduler data */
	runtime_api_async_exec_func_t  func;         /*!< The function we should run for this task */
} sched_async_task_t;

/**
 * @brief Initialize the async subsystem
 * @return status code
 **/
int sched_async_init();

/**
 * @brief Fianlize the async subsystem
 * @return status code
 **/
int sched_async_finalize();

/**
 * @brief set how many thread can we have in the async thread pool
 * @param n the number of thread 
 * @return status 
 **/
int sched_async_set_thread_pool_size(uint32_t n);

/**
 * @brief Post as new task to the async task pool, and wait one of the async thread picking up the task
 * @param task The task we want to post
 * @note  This function should be called from the worker thread only
 * @return status code
 **/
int sched_async_post_task(sched_async_task_t task);

#endif /* __SCHED_ASYNC_H__ */
