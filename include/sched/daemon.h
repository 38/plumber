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
 * @brief The iterator used to traverse the running daemon liist
 **/
typedef struct _sched_daemon_iter_t sched_daemon_iter_t;

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
 * @brief Start enumerate the daemon list
 * @return The newly created daemon info object, NULL on error
 **/
sched_daemon_iter_t* sched_daemon_list_begin();

/**
 * @brief Move to the next daemon in the daemon info list
 * @param info current daemon list
 * @param name The buffer used to return name, this will allocate memory
 * @param pid The buffer used to return pid
 * @return How many daemon info has been returned, 0 if we enumerated all. Error code on error cases
 * @note When it returns 0, the iterator will be automatically disposed. The name is a new reference, thus the
 *       caller needs to dispose the name string after use
 **/
int sched_daemon_list_next(sched_daemon_iter_t* iter, char** name, int* pid);

/**
 * @brief Try to ping a daemon, if test the daemon is alive
 * @param daemon_name the Daemon name
 * @return ping result 1 for Ok, otherwise 0
 **/
int sched_daemon_ping(const char* daemon_name);

/**
 * @brief The stop the daemon with deamon name
 * @param daemon_name The name of the Deamon
 * @return status code
 **/
int sched_daemon_stop(const char* daemon_name);

#endif /* __SCHED_DAEMON_H__ */
