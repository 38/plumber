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

/**
 * @brief Make current application a daemon, it will do the normal daemonize,
 *        create the controlling socket and pid file. When runtime.daemon.id == ""
 *        then this function does nothing
 * @return status code
 **/
int sched_daemon_daemonize();

/**
 * @brief The stop the daemon with deamon name
 * @param daemon_name The name of the Deamon
 * @return status code
 **/
int sched_daemon_stop(const char* daemon_name);

/**
 * @brief Get the list of current running Plumber daemon list
 * @param ret The return array
 * @return The size of array, error code on error cases
 **/
int sched_daemon_list(char ** ret);

#endif /* __SCHED_DAEMON_H__ */
