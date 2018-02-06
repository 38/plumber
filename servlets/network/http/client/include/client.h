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

typedef struct {
	const char*                  uri;        /*!< The URI to the resource to request */
	int                          priority;   /*!< The priority of the request */
	client_request_setup_func_t  setup;      /*!< The setup callback function */
	void*                        setup_data; /*!< The setup data */

	uint32_t                     save_header:1;  /*!< If we want to save header for the request */

	async_handle_t*              async_handle;   /*!< The async handle */

	char*                        result;     /*!< The buffer to return result */
	size_t                       result_sz; /*!< The result size buffer */

	char*                        header;    /*!< The buffer to return header */
	size_t                       header_sz; /*!< The header size buffer */

	CURLcode                     curl_rc;    /*!< The CURL return code buffer */

	int                          status_code; /*!< THe status code */
} client_request_t;

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
 * @brief Add a new request to the request queue
 * @param req The request to add
 * @param block If we need wait until the request being success fully added
 * @param before_add_cb The callback function called before we eventually add the request
 * @param cb_data The callback data
 * @return number of request has been enqueued, or error code
 **/
int client_add_request(client_request_t* req, int block, int (*before_add_cb)(void*), void* cb_data);

/**
 * @biref Notify the curl handle when the RLS buffer gets ready
 * @todo Impl
 * @param curl_handle The curl handle to notify
 * @return status code
 **/
int client_notify_write_ready(CURL* curl_handle);

#endif /* __NETWORK_HTTP_CLIENT_CLIENT_H__ */
