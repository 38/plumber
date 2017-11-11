/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the driver is the high level code of scheduler part.
 *        It's responsible for responding a request and instantiate
 *        tasks, etc.
 *        And it also provides a main function that is the main loop
 *        for the scheduler
 * @file step.c
 **/
#ifndef __PLUMBER_SCHED_STEP_H__
#define __PLUMBER_SCHED_STEP_H__

/**
 * @brief take the next step
 * @param type the pipe type between nodes
 * @param stc the scheduler task context for current thread
 * @return status code <br/>
 *         0 nothing to do <br/>
 *         negative for error case <br/>
 *         otherwise take one step
 **/
int sched_step_next(sched_task_context_t* stc, itc_module_type_t type);

/**
 * @brief get the current request scope object
 * @return the current request local scope, NULL if the program stack is outside of a task or error case
 **/
sched_rscope_t* sched_step_current_scope(void);

#endif /* __PLUMBER_SCHED_DRIVER_H__ */
