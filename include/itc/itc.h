/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file itc.h
 * @brief the ITC(Inter-Task Communication) system, or the pipe runtime
 * @details The ITC subsystem is the system that actually create, manage and manipuate all pipelines.
 *          Also supports the module infrastructure and dealing with the events and pipes.
 *          And expose a pipe handle to other parts of Plumber.
 **/
#include <stddef.h>

#ifndef __PLUMBER_ITC_H__
#define __PLUMBER_ITC_H__

#include <itc/module_types.h>

#include <itc/module.h>

#include <itc/equeue.h>

#include <itc/eloop.h>

#include <itc/modtab.h>

#include <itc/binary.h>

/**
 * @brief initialize the inter-task communication subsystem
 * @return status code
 **/
int itc_init(void);

/**
 * @brief finalize the inter-task communication subsystem
 * @return status code
 **/
int itc_finalize(void);

#endif /* __PLUMBER_ITC_H__ */
