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
int sched_daemon_init(void);

/**
 * @brief finalizate the daemon support
 * @return status code
 **/
int sched_daemon_finalize(void);

/**
 * @brief Make current application a daemon, it will do the normal daemonize,
 *        create the controlling socket and pid file. When runtime.daemon.id == ""
 *        then this function does nothing
 * @param fork_twice If we need fork twice
 * @note The fork_twice option is actually used when we are in the REPL mode, because
 *       we don't want the REPL shell exit after the daemon gets started.
 *       Thus we need to fork the starter process first time and then finish the remining
 *       part. At the same time, the REPL process just return successfully.
 * @return For the fork once mode, it returns 1 or error code in the daemon process and the parent process will exit inside the function <br/>
 *         For the fork twice mode, the REPL process will receive 0 or error code and daemon process will receive 1 or error code
 **/
int sched_daemon_daemonize(int fork_twice);

/**
 * @brief Start enumerate the daemon list
 * @return The newly created daemon info object, NULL on error
 **/
sched_daemon_iter_t* sched_daemon_list_begin(void);

/**
 * @brief Move to the next daemon in the daemon info list
 * @param iter current daemon list iterator
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

/**
 * @brief Read the control socket
 * @note This function should be called from the daemon side
 * @return status code
 **/
int sched_daemon_read_control_sock(void);

/**
 * @brief Reload the daemon with the given service object
 * @param daemon_name The name of the daemon
 * @param service The service object
 * @return status code
 **/
int sched_daemon_reload(const char* daemon_name, const sched_service_t* service);

#endif /* __SCHED_DAEMON_H__ */
