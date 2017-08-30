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
 * @note  This function should be called from the worker thread only <br/> 
 *        This function should assume that the sched_task_t has been instatiated already, so the task->exec_task
 *        is NULL the behavior is undefined. <br/>
 *        This function will create all the async task companions and run the async_setup task <br/>
 *        If this function returns successfully, the task->exec_task will be disposed and the async_cleanup task 
 *        will be assigned to task->exec_task. <br/>
 *        Once the woker thread gets notified from the event queue about async task completion, it can call task->exec_task
 *        to finalize the cleanup async operation.
 * @return status code, it's possible that the task->exec_task has been disposed completely, in this case ERROR_CODE_OT is returned
 **/
int sched_async_task_post(sched_loop_t* loop, sched_task_t* task);

/**
 * @brief Dispose the async task handle
 * @param handle The task handle to dispose
 * @return ststua code
 **/
int sched_async_handle_dispose(runtime_api_async_handle_t* handle);

/**
 * @brief Get current status code of the async task
 * @param handle The async handle
 * @param resbuf The result buffer 
 * @note Because both the async task handle and the fucntion itself uses the error code convention
 *       so it's impossible to distiuish the function failure and the task failure if we returns the status 
 *       code directly. So we need pass in a result buffer just for the task status so that we can distinguish
 *       the function faiulre and the ask failure.
 * @return status code
 **/
int sched_async_handle_get_status_code(runtime_api_async_handle_t* handle, int* resbuf);

/**
 * @brief Make the async handle to the await mode, which means even though the async task has been done
 *        but we simply done send the task compeltion event util the async_cntl function has been called 
 *        for the task compeletion
 * @param handle The task handle
 * @todo  Actually we have an issue about what if the waiting task never calls the async_cntl funciton. 
 *        The best way for us to address is having a time limit for the task. So the time limit for the task
 *        becomes a very intersting thing, because it may means we need to cancel this.
 * @return status code
 **/
int sched_async_handle_await_set(runtime_api_async_handle_t* handle);

/**
 * @brief The function that notify the await function has been completed
 * @param handle the task handle
 * @param status The status code we want to pass to the task
 * @return status code
 **/
int sched_async_handle_await_complete(runtime_api_async_handle_t* handle, int status);

#endif /* __SCHED_ASYNC_H__ */
