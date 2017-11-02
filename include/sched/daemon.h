/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The daemon and nonstop deployment support
 * @file sched/daemon.h
 **/
#ifndef __SCHED_DAEMON_H__
#define __SCHED_DAEMON_H__

/**
 * @brief intitialize the daemon support
 * @return status code
 **/
int sched_daemon_init();

/**
 * @brief finalizate the daemon support
 * @return status code
 **/
int sched_daemon_finalize();

#endif /* __SCHED_DAEMON_H__ */
