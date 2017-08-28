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
 * @brief Start all the async thread and make the async task processor ready to run
 * @return status code
 **/
int sched_async_start();

/**
 * @biref Kill all the async thread and cleanup all the resource it occupies
 * @return status code
 **/
int sched_async_kill();

/**
 * @brief Post as new task to the async task pool, and wait one of the async thread picking up the task
 * @param loop The scheduler loop which posts this task 
 * @param task The task we want to post
 * @note  This function should be called from the worker thread only
 * @return status code
 **/
int sched_async_task_post(sched_loop_t* loop, sched_task_t* task);

#endif /* __SCHED_ASYNC_H__ */
