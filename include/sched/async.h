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
 * @note    The task event may not be sucessfully dispatched, so we need something to make sure it doesn't
 *          block the dispatcher when this happend first time.
 *          If an async task's downstream blocks the schceduler thread, and during the async task running,
 *          a lot of requests have been sent to the scheduler, and those tasks will create another async task
 *          as an result, there will be a lot of async task compeletion event raised after times.
 *          The worst case is, the event has filled up the scheduler event queue and global event queue.
 *          As the result of this worst case, no scheduler thread is able to move on.
 *
 *          The desigin decision of this is, we currently just ignore this situation, because, it's really rare
 *          that we could have too many task that block all the queue. In addition, because the service graph has
 *          finite nodes, and the size of scheduler queue is finite. Which means the async task can be blocked in
 *          the event queue can not be larger than a constant. This means at least we can avoid this by adjusting
 *          the event queue size.
 *
 *          TODO: But ideally we could have some mechanism to address this even tough the event queue isn't large engouh
 *          to avoid this. It seems the best candicate of the mechanism is allow each task has a time limit, and if the
 *          task timed out it will be killed. However, this introduces an serious issue about the how to clean the killed
 *          task. Since we share the address space, we don't have a chance to dispose all the allocated memory. However,
 *          we can provide a way for the servlet author to address this.
 *
 *          It seems we can have a cleanup label in the exec function after the return clause. And we can call a API function
 *          reigster this label and once we decide to kill it, the thread will jump to that label. Although for javascript,
 *          python and other GC language, it could have a lot of work to do. But at least when a native servlet is killed
 *          it has the chance to do the correct thing.
 *
 *          After we have the timeout mechanism, we not only solve the async dispatching problem, but also the long running
 *          task won't make the entire server freeze anymore.
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
 * @return status code, it's possible that the task->exec_task has been replaced by another task (cleanup task)
 *         However, the function guarantee that task->exec_task is stil valid.
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
int sched_async_handle_status_code(runtime_api_async_handle_t* handle, int* resbuf);

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
int sched_async_handle_set_await(runtime_api_async_handle_t* handle);

/**
 * @brief The function that notify the await function has been completed
 * @param handle the task handle
 * @param status The status code we want to pass to the task
 * @return status code
 **/
int sched_async_handle_await_complete(runtime_api_async_handle_t* handle, int status);

/**
 * @brief Cancel a started async task, which means we prevent the async_exec task from being
 *        posted to the async queue
 * @param handle The async handle
 * @param status The status code we want to set to the async handle
 * @return status code
 **/
int sched_async_handle_cancel(runtime_api_async_handle_t* handle, int status);

/**
 * @brief The actual handle cntl implemnetaiton
 * @param handle The async handle
 * @param opcode The opcode
 * @return ap The va list
 * @return status code
 **/
int sched_async_handle_cntl(runtime_api_async_handle_t* handle, uint32_t opcode, va_list ap);

/**
 * @brief Create a new fake async handle
 * @note When we call the async servlet from the pstest, it's possible that the servlet calls a async_cntl
 *       during the time, this actually discard all the request beside the waiting mode related operations
 * @return The fake handle
 **/
runtime_api_async_handle_t* sched_async_fake_handle_new();

/**
 * @brief Indicates if the fake handle has been compelted
 * @param handle The handle to dispose
 * @return result or status code
 **/
int sched_async_fake_handle_completed(const runtime_api_async_handle_t* handle);

/**
 * @brief dispose a fake async handle
 * @param handle The handle to dispose
 * @return status code
 **/
int sched_async_fake_handle_free(runtime_api_async_handle_t* handle);

#endif /* __SCHED_ASYNC_H__ */
