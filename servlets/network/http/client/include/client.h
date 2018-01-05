/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The client wrapper for the HTTP client servlet
 * @file network/http/client/include/client.h
 **/
#ifndef __NETWORK_HTTP_CLIENT_CLIENT_H__
#define __NETWORK_HTTP_CLIENT_CLIENT_H__

/**
 * @brief The function used to setup the CURL object for the request
 * @note This function actually calls from the client thread, since the
 *       CURL library isn't thread safe. And this pattern eliminates the
 *       mutex.
 *       In this function, the URL will be setup already
 * @param handle The curl object to setup
 * @param data The additional data needs to pass in to the callback
 * @return status code
 **/
typedef int (*client_request_setup_func_t)(CURL* handle, void* data);

/**
 * @brief Initialize the client
 * @param queue_size the *minimun* size of queue this servlet requested for, the actual
 *                   value will not be smaller than this value
 * @param parallel_limit The lower bound of the numer of parallel request at the same time per thread
 * @param num_threads The lower bound of the number of threads
 * @return status code
 * @note Since the client threads are shared among all the client servlet instances, and each of the
 *       servlet might request for different queue size, parallel limit and number of threads. 
 *       In this case, the client library will satisify requests of all the threads
 **/
int client_init(uint32_t queue_size, uint32_t parallel_limit, uint32_t num_threads);

/**
 * @brief Finalize the client
 * @return status code
 * @note This function actually decrement the initialize counter and when the intialize
 *       counter reached 0, the actual finalization procedure will be triggerred.
 **/
int client_finalize(void);

/**
 * @brief Set the user-agent string of this client
 * @param ua The user agent string
 * @return status
 **/
int client_set_user_agent_string(const char* ua);

/**
 * @brief Add a new request to the request queue
 * @param url The url to request
 * @param handle The async processing handle
 * @param block If the funtion would block the execution of current thread.
 * @param setup The setup callback, NULL if there's no need for setup
 * @param setup_data The data needs to be pass in to the setup callback
 * @param priority The priority of this request
 * @return status code
 **/
int client_add_request(const char* url, async_handle_t* handle, int priority, int block, client_request_setup_func_t setup, void* setup_data);

#endif /* __NETWORK_HTTP_CLIENT_CLIENT_H__ */
