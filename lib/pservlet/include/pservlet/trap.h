/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

/**
 * @brief the function that used for debug and testing purpose
 * @file trap.h
 **/
#ifndef __TRAP_H__
#define __TRAP_H__

/**
 * @brief make the task goes into a trap which makes the execution accross the Servlet-Framework Boundary
 * @param id the trap id
 * @return nothing
 **/
void trap(int id)
	__attribute__((visibility ("hidden")));

#endif /* __TRAP_H__ */
