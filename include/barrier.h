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

#endif
