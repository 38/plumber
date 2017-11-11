/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The plumber built-in profiler utilies
 * @note the profiler utilities will be controlled by the
 *       variable scheduler.prof.enabled = 0 / 1
 * @file sched/prof.h
 **/
#ifndef __PLUMBER_SCHED_PROF_H__
#define __PLUMBER_SCHED_PROF_H__
/**
 * @brief the data structure used in the profiler file
 **/
typedef struct {
	uint32_t                thread;  /*!< the thread id */
	sched_service_node_id_t node;    /*!< the node id */
	uint64_t                time;    /*!< the time used by this node */
	uint64_t                count;   /*!< the number of execution of this node */
} sched_prof_record_t;

/**
 * @brief the profiler object
 **/
typedef struct _sched_prof_t sched_prof_t;

/**
 * @brief the global initialization for the profiler
 * @return status code
 **/
int sched_prof_init(void);

/**
 * @brief the global finalization for the profiler
 * @return status code
 **/
int sched_prof_finalize(void);

/**
 * @brief create a new profiler for the given service
 * @note if the global variable shows we should disable the profiler, it will still success
 *       but no profiler creation
 * @param service the target service
 * @param result the result buffer
 * @return status code
 **/
int sched_prof_new(const sched_service_t* service, sched_prof_t** result);

/**
 * @brief dispose a used profiler object
 * @param prof the profiler to dispose
 * @return status code
 **/
int sched_prof_free(sched_prof_t* prof);


/**
 * @brief start the timer for the node
 * @param prof the profiler
 * @param node current node
 * @return status code
 **/
int sched_prof_start_timer(sched_prof_t* prof, sched_service_node_id_t node);

/**
 * @brief stop the timer for the node
 * @param prof the profiler
 * @return status code
 **/
int sched_prof_stop_timer(sched_prof_t* prof);

/**
 * @brief flush the profiling data and reset the accumulator for current thread
 * @param prof the profiler
 * @return status code
 **/
int sched_prof_flush(sched_prof_t* prof);

#endif /* __PLUMBER_SCHED_PROF_H__ */
