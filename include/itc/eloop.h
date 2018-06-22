/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The Event Loop implementation
 * @file eloop.h
 **/
#ifndef __PLUMBER_ITC_ELOOP_H__
#define __PLUMBER_ITC_ELOOP_H__

/**
 * @brief the thread local indicates if this thread get killed
 **/
extern __thread uint32_t itc_eloop_thread_killed;
/**
 * @brief initialize the Event loop subsystem
 * @return status code
 **/
int itc_eloop_init(void);

/**
 * @brief finalize the event loop subsystem
 * @return status code
 **/
int itc_eloop_finalize(void);

/**
 * @brief Start the event loops for all event accepting module instances <br/>
 * @details When this function gets called, it will enumerate all the event accepting module instances
 *         and start isolate event loops for each module isntances.
 * @param pipe_param A pipe initialization parameter. If this is not NULL, the eloop will setup the pipe_param
 *        for all the thread that is starting in a **thread-safe fashion**.
 * @note Although it's possible for the scheduler to change the pipe init param after the loop started.
 *       However, *This is not thread-safe* (See the comment in the code). The entire reason for allowing 
 *       data race in that point is we assume the need for changing the thread init param dynamically is 
 *       very rare. So if we add a lock at this point, it's completely an overkill. 
 *       But be careful when the itc_eloop_set_accept_param or itc_eloop_set_all_accept_param need to be used.
 * @return status code
 **/
int itc_eloop_start(const itc_module_pipe_param_t* pipe_param);

/**
 * @brief set the accept param for a module instance
 * @details when the event loop accept param is set, all the event loop will use the given pipe param to
 *         accept new event comes from the moudle instance
 * @param module the module ID
 * @param param the param used to accept request
 * @return status code
 **/
int itc_eloop_set_accept_param(itc_module_type_t module, itc_module_pipe_param_t param);

/**
 * @brief set the event accept param for all the event accepting module instance
 * @param param the param used to accept request
 * @return status code
 **/
int itc_eloop_set_all_accept_param(itc_module_pipe_param_t param);

#endif /* __PLUMBER_ITC_ELOOP_H__ */
