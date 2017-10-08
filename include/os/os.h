/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The operating system related functions
 * @file os/os.h
 **/
#include <constants.h>
#ifndef __OS_H__
#define __OS_H__
#if !defined(__LINUX__) && \
    !defined(__DARWIN__)
#	error("Unsupported operating sytsem type")
#endif
#include <os/const.h>
#include <os/event.h>
#endif /* __OS_H__ */
