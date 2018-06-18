/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the memory barrier
 * @file barrier.h
 **/

#ifndef __PLUMBER_BARRIER_H__
#define __PLUMBER_BARRIER_H__

/**
 * @brief the instruction barrier. <br/>
 *        This is the macro that can insert an assembler code, so that
 *        make sure the optimizer won't reorder the instruction accross
 *        the barrier
 **/
#define BARRIER() asm volatile ("" : : : "memory")

#define BARRIER_FULL() __sync_synchronize()

#if defined(__i386__) || defined(__amd64__)

#	define BARRIER_LL() BARRIER()

#	define BARRIER_LS() BARRIER()

#	define BARRIER_SS() BARRIER()

#	define BARRIER_SL() BARRIER_FULL()

#elif defined(__arm32__)

#	define BARRIER_LL() BARRIER_FULL()

#	define BARRIER_LS() BARRIER_FULL()

#	define BARRIER_SS() BARRIER_FULL()

#	define BARRIER_SL() BARRIER_FULL()

#else 

#	error("Unsupported CPU type");

#endif

#endif
