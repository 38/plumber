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
 * @brief The type used to describe a task
 * @todo Determine if we need a timeout
 **/
typedef struct {
	void*                          data;         /*!< The data used by this task */
	void*                          thread_data;  /*!< The source task scheduler thread context */
	void*                          sched_data;   /*!< The additional scheduler data */
	void*                          servlet_ctx;  /*!< The servlet context data */
	/**
	 * @brief Initialize the async task
	 * @param handle The async task handle
	 * @param async_buf The async data buffer we need to fill
	 * @param ctxbuf The servlet context buffer
	 * @return status code
	 **/
	int (*init)(const void* handle, void* async_buf, void* ctxbuf);
	/**
	 * @brief Actual execution callback, which we should run from the async processing thread
	 * @note In this function, we don't have any plumber API access besides async_cntl with the handle
	 * @param handle The task handle
	 * @param async_buf The async buffer
	 * @return status code
	 **/
	int (*exec)(const void* handle, void* async_buf);
	/**
	 * @brief Do the cleanup after the async task is done
	 * @param async_buf The async buffer
	 * @param ctxbuf The context buffer
	 * @return status code
	 **/
	int (*cleanup)(void* async_buf, void* ctxbuf);
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
 * @param task The task we want to post
 * @note  This function should be called from the worker thread only
 * @return status code
 **/
int sched_async_post_task(sched_async_task_t task);

#endif /* __SCHED_ASYNC_H__ */
