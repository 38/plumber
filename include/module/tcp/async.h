/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the async loop for the TCP pipe
 * @note Before we do this, the pipe_write call is synced by definition. In TCP case
 *       it will simulate a blocking IO based on the non-blocking socket. It's OK in
 *       most of the case, because either the file is small enough or the connection
 *       is fast enough. But when requesting a large file over slow connection, this
 *       method will make the working thread freeze and because the connection is
 *       slow, most of the write attempts will be failed, so the working thread turns
 *       into a polling state. The async loop is to solve this by the epoll technique. <br/>
 *
 *       The TCP module is able to transfer the ownership of a connection to the async
 *       by registering the connection object to the async loop. At the same time the
 *       module is still able to performe write (note that the thread safety should be
 *       considered if the data handler is going to be changed, but the race condition
 *       is limited between the asnyc callbacks and the TCP module's write function)
 *       TCP module is supposed not to request the connection pool to release the
 *       directly connection. Instead, it should use the asnyc loop API to request a
 *       connection release from the async loop.
 * @file module/tcp/async.h
 **/
#ifndef __MODULE_TCP_ASYNC__
#define __MODULE_TCP_ASYNC__

/**
 * @brief the incompete type for an asnyc loop
 **/
typedef struct _module_tcp_async_loop_t module_tcp_async_loop_t;

/**
 * @brief the data source callback
 * @param conn_id the id of connection object invokes this function
 * @param buffer the buffer uesd to pass data
 * @param size the size of the buffer
 * @param caller the caller async object
 * @return number of bytes copied to buffer
 **/
typedef size_t (*module_tcp_async_write_data_func_t)(uint32_t conn_id, void* buffer, size_t size, module_tcp_async_loop_t* caller);

/**
 * @brief the callback function to dispose an asnyc write handle
 * @param conn_id the id of connection object invokes this function
 * @param caller the caller async object
 * @return status code
 **/
typedef int (*module_tcp_async_write_cleanup_func_t)(uint32_t conn_id, module_tcp_async_loop_t* caller);

/**
 * @brief the callback function used to handle the write error
 * @param conn_id the id of the connection object invokes this function
 * @param caller the caller async object
 * @return status code
 **/
typedef int (*module_tcp_async_write_error_func_t)(uint32_t conn_id, module_tcp_async_loop_t* caller);

/**
 * @brief create and start a tcp asnyc loop
 * @param pool_size the connection pool size, which is used as the maximum size of the async object it can hold
 * @param event_size the size of the event buffer
 * @param ttl the max wait time for each connection
 * @param write the mocked write function (only for testing purpose. otherwise pass NULL)
 * @return the newly created async loop, NULL on error case
 **/
module_tcp_async_loop_t* module_tcp_async_loop_new(uint32_t pool_size, uint32_t event_size, time_t ttl, ssize_t (*write)(int, const void*, size_t));

/**
 * @brief stop the async loop and dispose all the resources
 * @param loop the target async loop
 * @return status code
 **/
int module_tcp_async_loop_free(module_tcp_async_loop_t* loop);

/**
 * @brief register new async write object assocaited to the givein conn_id
 * @param loop the target async loop
 * @param conn_id the connection object ID from the connection pool
 * @param fd the underlying socket fd
 * @param buf_size the async write buffer size
 * @param get_data the callback function that feeds data
 * @param cleanup  the callback function that do cleanup
 * @param onerror  the error handler
 * @param handle   the caller-defined handle for this async
 * @return the status code
 **/
int module_tcp_async_write_register(module_tcp_async_loop_t* loop,
                                    uint32_t conn_id, int fd, size_t buf_size,
                                    module_tcp_async_write_data_func_t get_data,
                                    module_tcp_async_write_cleanup_func_t cleanup,
                                    module_tcp_async_write_error_func_t onerror,
                                    void* handle);

/**
 * @brief notify the async write object for data ready event
 * @param loop the target async loop
 * @param conn_id the target connection id which is performing the async operation
 * @return the status code
 **/
int module_tcp_async_write_data_ready(module_tcp_async_loop_t* loop, uint32_t conn_id);

/**
 * @brief notify the async data source has no more data
 * @param loop the target async loop
 * @param conn_id the target connection id which is performing the async operation
 * @return the status code
 **/
int module_tcp_async_write_data_ends(module_tcp_async_loop_t* loop, uint32_t conn_id);

/**
 * @brief get the data handle of a conn_id
 * @param loop the target async loop
 * @param conn_id the target connection id which is performing the async operation
 * @todo currently we passing the loop object via a function param.
 *       But ideally we should make this passed with a thread-local, so that the
 *       callback fuction is able to access this even though the async loop is not
 *       visibile to it
 * @return the data handle
 **/
void* module_tcp_async_get_data_handle(module_tcp_async_loop_t* loop, uint32_t conn_id);

#endif /*__MODULE_TCP_ASYNC__*/
