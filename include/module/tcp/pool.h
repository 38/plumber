/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the connection pool actually manages the connection between client and server
 * @file tcp/pool.h
 **/
#ifndef __PLUMBER_MODULE_TCP_POOL__
#define __PLUMBER_MODULE_TCP_POOL__

/**
 * @brief indicates that we want the framework automatically decide what to do
 * @details This means, if we pass a NULL pointer as data, the pool will automatically put mark the connection object as inactive <br/>
 *          If there's a data pointer passed in, the pool will automatically  mark the connection object as wait for read state. <br/>
 *          This mode is useful when we want the pool be smart and we only wants to perserve ununsed data for furture request. <br/>
 *          And in this mode, we do not need to provide a dispose function for the data,
 *          because all the inactive connection object must have a NULL data pointer.<br/>
 *          But if we want to peserve state data other than the ununsed bytes, this property is not guareenteed, so we must pass a
 *          dispose_data pointer.
 * @note in this mode, you must make sure you have seen EAGAIN before you pass NULL as data
 **/
#define MODULE_TCP_POOL_RELEASE_MODE_AUTO 0
/**
 * @brief indicates that we want to force the connection pool close this connection
 **/
#define MODULE_TCP_POOL_RELEASE_MODE_PURGE 1

/**
 * @brief indicates that we want to mark this connection as "wait for read"
 **/
#define MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_READ 2
/**
 * @brief indicates that we want to mark this connection as "wait for data"
 * @note before do this, make sure you have seen EAGAIN
 **/
#define MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_DATA 3

/**
 * @brief the TCP connection pool object
 **/
typedef struct _module_tcp_pool_t module_tcp_pool_t;

/**
 * @brief the data structure that represents a connection pool configuration
 **/
typedef struct {
	uint16_t    port;       /*!< the listening port */
	time_t      ttl;        /*!< the minimum time for a inactive connection to hold */
	time_t      min_timeout;/*!< the minimum time out vlaue */
	int         tcp_backlog;/*!< the backlog value for the tcp connection */
	int         reuseaddr;  /*!< indicates if we want to reuse the binding address */
	int         ipv6;       /*!< indicates we want to bind to a ipv6 address */
	uint32_t    size;       /*!< the maximum number of connections the pool can hold*/
	const char* bind_addr;  /*!< the bind address */
	size_t      event_size; /*!< the size for the event array */
	uint32_t    accept_retry_interval;  /*!< The most time we sleep if we can not accept the socket (This is useful when we used up the FD) */
	int         (*dispose_data)(void*); /* the callback function used to dispose the unused data */
} module_tcp_pool_configure_t;

/**
 * @brief the data structure that use to return a active connection
 **/
typedef struct {
	int      fd;    /*!< the file descriptor */
	uint32_t idx;   /*!< the connection pool internal index for the connection */
	void*    data;  /*!< the addtional data previously attached to the fd */
} module_tcp_pool_conninfo_t;

/**
 * @brief create a new TCP connection pool
 * @return the newly created connection pool object
 **/
module_tcp_pool_t* module_tcp_pool_new(void);

/**
 * @brief Fork an existing TCP pool.
 * @details This will create another connection pool listening to the same TCP port.
 *          This is used when the event loop becomes a bottelneck, thus we want to
 *          use multiple event loop for the same socket FD
 * @return the newly created connection pool object, or NULL on error
 **/
module_tcp_pool_t* module_tcp_pool_fork(module_tcp_pool_t* pool);

/**
 * @brief Get how many forks has been created under this master pool, 0 if the pool itself is a forked pool
 * @param pool The pool to examine
 * @return The number of forks or error code
 **/
int module_tcp_pool_num_forks(const module_tcp_pool_t* pool);

/**
 * @brief dispose the used TCP connection pool
 * @param pool the connection pool to dispose
 * @return status code
 **/
int module_tcp_pool_free(module_tcp_pool_t* pool);

/**
 * @brief Set a TCP connection pool configuration
 * @param pool the target connection pool object
 * @param conf the configuration
 * @return the status code
 **/
int module_tcp_pool_configure(module_tcp_pool_t* pool, const module_tcp_pool_configure_t* conf);

/**
 * @brief get an active connection from the connection pool
 * @param pool the connection pool object
 * @param buf the buffer for result
 * @return status code
 **/
int module_tcp_pool_connection_get(module_tcp_pool_t* pool, module_tcp_pool_conninfo_t* buf);

/**
 * @brief return this connection to the pool, this function will append a release queue message
 *        and notify the event loop to handle the actual release operation
 * @param pool the connection pool object
 * @param id  the connection pool internal index number for the connection
 * @param data the additional data attached to this fd
 * @param mode the mode selection
 * @return status code
 **/
int module_tcp_pool_connection_release(module_tcp_pool_t* pool, uint32_t id, void* data, int mode);

/**
 * @brief change the state of pool to ready to exit
 * @param pool the target connection pool
 * @return nothing
 **/
void module_tcp_pool_loop_killed(module_tcp_pool_t* pool);
/**
 * @brief manually call the poll event function
 * @note this function should only be used in test cases. <br/>
 *       The reason to have this function is in some cases, we are able to use
 *       signle threaded mode (which event loop and scheduler are runing in the
 *       same thread), in this case, if a async write involved. <br/>
 *       Dead lock is possible in the case the async write ends after the pipe disposed,
 *       and the main loop is performing a blocking operation on others. In this case,
 *       we should manually poll the TCP pool to make sure there's no dead lock. <br/>
 *       See test case for TCP module for details. <br/>
 * @param pool the target connection pool object
 * @return status code
 **/
int module_tcp_pool_poll_event(module_tcp_pool_t* pool);

#endif /*__PLUMBER_MODULE_TCP_POOL__ */
