/**
 * Copyright (C) 2017, Hao Hou
 **/

#ifndef __PLUMBER_SCHED_LOOP_H__
#define __PLUMBER_SCHED_LOOP_H__
/**
 * @brief the type used to describe the type of the event message
 * @note this is useful when we have more things that can wake up the scheduler thread
 **/
typedef enum {
	SCHED_LOOP_EVENT_TYPE_ITC    /*!< Indicates this is a ITC event, typically a incoming request */
} sched_loop_event_type_t;

/**
 * @brief descirbes a loop event
 **/
typedef struct {
	sched_loop_event_type_t type;  /*!< the type of the event */
	union {
		itc_equeue_event_t itc; /*!< a module event */
	};
} sched_loop_event_t;

/**
 * @brief start scheduler loop
 * @param service the service to run
 * @return status code
 **/
int sched_loop_start(const sched_service_t* service);

/**
 * @brief kill the scheduler loop
 * @param no_error if this param is non-zero vlaue, it means no error even though the scheduler
 *        loop haven't been started yet
 * @return status code
 **/
int sched_loop_kill(int no_error);

/**
 * @brief set the number of thread that should be used
 * @param n the number of thread
 * @return status code
 **/
int sched_loop_set_nthreads(uint32_t n);

/**
 * @brief set the scheduler event queue size
 * @param size the target size
 * @return status code
 **/
int sched_loop_set_queue_size(uint32_t size);

/**
 * @brief initialize this file
 * @return status code
 **/
int sched_loop_init();

/**
 * @brief finalize this file
 * @return status code
 **/
int sched_loop_finalize();


#endif /* __PLUMBER_SCHED_LOOP_H__ */
