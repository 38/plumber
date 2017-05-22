/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The top level include file for the scheduler
 * @file sched.h
 **/

#ifndef __PLUMBER_SCHED_H__
#define __PLUMBER_SCHED_H__
#include <sched/service.h>
#include <sched/rscope.h>
#include <sched/task.h>
#include <sched/step.h>
#include <sched/loop.h>
#include <sched/cnode.h>
#include <sched/prof.h>
#include <sched/type.h>
/**
 * @brief intitialize the scheduler part
 * @return status code
 **/
int sched_init();

/**
 * @brief finalizate the scheduler
 * @return status code
 **/
int sched_finalize();

#endif /** __PLUMBER_SCHED_H__ */
