/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <barrier.h>
#include <module/tcp/pool.h>
#include <utils/log.h>
#include <utils/bitmask.h>
#include <error.h>
#include <arch/arch.h>
#include <os/os.h>

/**
 * @brief the message in the message queue
 **/
enum {
	_QM_DEACTIVATE,   /*!< indicates we should deactivate the object */
	_QM_CLOSE,        /*!< indicates we should close the object */
	_QM_CHECKIN,      /*!< indicates we should check-in the object */
};
/**
 * @brief a release queue message
 **/
typedef struct {
	uint8_t    type;     /*!< the message type */
	uint32_t   id;       /*!< the target connection object id */
	void*      data; /*!< the data atatched to the queue message, used in the release messages */
} _queue_message_t;

/**
 * @brief the node in the pool
 **/
typedef struct {
	void*      data;   /*!< the additional data attached to this fd */
	uint32_t   id;     /*!< the internal id that is used to identify the connection */
	int        fd;     /*!< the file descriptor */
	time_t     ts;     /*!< the timestamp when last active */
} _node_t;

/**
 * @brief the internal connection pool
 **/
typedef struct {
	bitmask_t*               bitmask;  /*!< the bitmask, a id allocator */
	uint32_t*                index;    /*!< the index array, use to indicates the index in the connection array */
	_queue_message_t*        queue;    /*!< the release request queue, the reason we have this is because we
	                                      should allow scheduler thread call the release function */
	pthread_mutex_t          q_mutex;  /*!< the global queue mutex, used for synchonization among working threads and write thread */
	uint32_t                 q_mask;   /*!< the queue mask is atuall queue_size - 1, we choose 2^n as the queue size, because in this case overflow won't be a problem */
	uint32_t                 q_front;  /*!< the next queue message to process */
	uint32_t                 q_rear;   /*!< the increment interger to identify the queue message */
	union {
		uint32_t             heap_limit;  /*!< range [0, heap_limit) in the connection array is used for the inactive heap */
		uint32_t             active_start;/*!< this is also the active start */
	};
	union {
		uint32_t             active_limit;/*!< range [heap_limit, active_limit) in the connection is used for the active connections */
		uint32_t             wait_start;  /*!< the head of start list */
	};
	union {
		uint32_t             wait_limit;  /*!< range [active_limit, heap_limit) in the connection is used for the waiting connections */
		uint32_t             nconnections;/*!< the number of connections */
	};
	_node_t*                 conn;        /*!< the connection array */
} _conn_info_t;

/**
 * @brief represents a pool instance
 **/
struct _module_tcp_pool_t{
	os_event_poll_t*            poll_obj;    /*!< The poll object */
	int                         socket_fd;   /*!< the listening fd */
	int                         event_fd;    /*!< the event fd for the release queue */
	module_tcp_pool_configure_t conf;        /*!< the module confiuration */
	_conn_info_t                conn_info;   /*!< the connection info object */
	struct sockaddr_in          saddr;       /*!< The socket addr */
	struct sockaddr_in6         saddr6;      /*!< The ipv6 socket addr */
	int                         loop_killed; /*!< indicates if the loop is gets killed */
	char                        addr_str_buf[INET6_ADDRSTRLEN];/*!< the buffer used to convert the network address to string */
};

/**
 * @brief prints the connection pool status
 * @param pool the connection pool object
 * @return nothing
 **/
static inline void _print_stat(const module_tcp_pool_t* pool)
{
	(void) pool;
	LOG_DEBUG("\tHeap:          [%"PRIu32", %"PRIu32")", 0,                            pool->conn_info.heap_limit);
	LOG_DEBUG("\tActive:        [%"PRIu32", %"PRIu32")", pool->conn_info.active_start, pool->conn_info.active_limit);
	LOG_DEBUG("\tInactive:      [%"PRIu32", %"PRIu32")", pool->conn_info.wait_start,   pool->conn_info.wait_limit);
}
/**
 * @brief release a connection object and close the corresponing FD
 * @param idx the index in the *connectin list* to release
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _release_connection_object(module_tcp_pool_t* pool, uint32_t idx)
{
	int rc = 0;
	if(close(pool->conn_info.conn[idx].fd) < 0)
	{
		LOG_ERROR_ERRNO("Cannot release the conneciton used by connection object %"PRIu32, pool->conn_info.conn[idx].id);
		rc = ERROR_CODE(int);
	}

	if(NULL != pool->conn_info.conn[idx].data)
	{
		LOG_DEBUG("External data has been attached to this connection object, trying to dispose");

		if(NULL == pool->conf.dispose_data)
		{
			LOG_ERROR("Cannot dispose the data field, dispose function must be provided"
			          "unless you use auto mode to release the connection object");
			rc = ERROR_CODE(int);
		}
		else
		{
			LOG_DEBUG("Calling dispose function for connection object %"PRIu32, pool->conn_info.conn[idx].id);
			if(pool->conf.dispose_data(pool->conn_info.conn[idx].data) == ERROR_CODE(int))
			{
				LOG_ERROR("Cannot dispose the data field, memory may be lost");
				rc = ERROR_CODE(int);
			}
		}
	}

	LOG_INFO("Connection object %"PRIu32" has been closed", idx);
	return rc;
}
/**
 * @brief initialize the internal connection buffer
 * @param capacity the capacity (max number of connection can support simultaneously) of the pool
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _init_conn_info(module_tcp_pool_t* pool, uint32_t capacity)
{
	uint32_t q_size = 1;
	uint32_t tmp = capacity;
	for(;tmp > 1; q_size <<= 1, tmp >>= 1);
	if(q_size < capacity) q_size <<= 1;

	if(pool->conn_info.bitmask != NULL || pool->conn_info.index != NULL || pool->conn_info.conn != NULL)
	    ERROR_RETURN_LOG(int, "cannot reinitialize the connection info object");

	if(NULL == (pool->conn_info.bitmask = bitmask_new(capacity)))
	    ERROR_RETURN_LOG(int, "cannot create new bitmask for the connection pool");

	if(NULL == (pool->conn_info.index = (uint32_t*)malloc(capacity * sizeof(uint32_t))))
	    ERROR_RETURN_LOG(int, "cannot allocate index array for the connection pool");

	if(NULL == (pool->conn_info.conn = (_node_t*)malloc(sizeof(_node_t) * capacity)))
	    ERROR_RETURN_LOG(int, "cannot allocate the connection array for the connection pool");

	if(NULL == (pool->conn_info.queue = (_queue_message_t*)malloc(sizeof(_queue_message_t) * q_size)))
	    ERROR_RETURN_LOG(int, "cannot allocate the release message queue");
	else
	    LOG_DEBUG("allocate the release message queue with %"PRIu32" slots", q_size);

	pool->conn_info.q_mask = q_size - 1;

	if(pthread_mutex_init(&pool->conn_info.q_mutex, NULL) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "cannot initialize the message queue mutex");

	pool->conn_info.heap_limit = pool->conn_info.active_limit = pool->conn_info.wait_limit = 0;

	return 0;

}

/**
 * @brief dispose the connection pool buffer
 * @return status code
 **/
static inline int _finalize_conn_info(module_tcp_pool_t* pool)
{
	int rc = 0;
	uint32_t i;
	for(i = 0; i < pool->conn_info.nconnections; i ++)
	    if(ERROR_CODE(int) == _release_connection_object(pool, i))
	        LOG_WARNING("Cannot release connection object, memory or FD leak is possible");

	if(NULL != pool->conn_info.bitmask) rc = bitmask_free(pool->conn_info.bitmask);

	if(NULL != pool->conn_info.index) free(pool->conn_info.index);

	if(NULL != pool->conn_info.conn) free(pool->conn_info.conn);

	if(NULL != pool->conn_info.queue) free(pool->conn_info.queue);

	pthread_mutex_destroy(&pool->conn_info.q_mutex);

	pool->conn_info.conn = NULL;
	pool->conn_info.bitmask = NULL;
	pool->conn_info.index = NULL;
	pool->conn_info.queue = NULL;

	return rc;
}

/**
 * @brief set the FD to non-blocking mode
 * @param fd the target fd
 * @return status code
 **/
static inline int _set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if(-1 == flags) ERROR_RETURN_LOG(int, "Cannot access the flags of the socket FD: %s", strerror(errno));

	flags |= O_NONBLOCK;
	if(-1 == fcntl(fd, F_SETFL, flags)) ERROR_RETURN_LOG(int, "Cannot update the flags of the socket FD: %s", strerror(errno));

	return 0;
}

module_tcp_pool_t* module_tcp_pool_new()
{
	module_tcp_pool_t* ret = (module_tcp_pool_t*)calloc(1, sizeof(module_tcp_pool_t));

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the connection pool object");

	ret->poll_obj = NULL;
	ret->event_fd = ERROR_CODE(int);

	/* Create the poll object  */
	if(NULL == (ret->poll_obj = os_event_poll_new()))
		ERROR_LOG_GOTO(ERR, "Cannot create poll object");

	/* Create the event fd used for the message queue */
	os_event_desc_t desc = {
		.type = OS_EVENT_TYPE_USER,
		.user = {
			.data = &ret->event_fd
		}
	};

	if(ERROR_CODE(int) == (ret->event_fd = os_event_poll_add(ret->poll_obj, &desc)))
		ERROR_LOG_GOTO(ERR, "Cannot create user space event");

	return ret;

ERR:
	if(NULL != ret->poll_obj) os_event_poll_free(ret->poll_obj);
	if(ret->event_fd >= 0) close(ret->event_fd);
	ret->poll_obj = NULL;

	free(ret);

	return NULL;
}

int module_tcp_pool_free(module_tcp_pool_t* pool)
{
	if(NULL == pool) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = _finalize_conn_info(pool);

	if(pool->socket_fd >= 0) close(pool->socket_fd);

	if(pool->event_fd >= 0) close(pool->event_fd);

	if(NULL != pool->poll_obj && ERROR_CODE(int) == os_event_poll_free(pool->poll_obj))
		rc = ERROR_CODE(int);

	free(pool);

	return rc;
}

/**
 * @brief initialize the socket so that the connection pool will start listing to the socket
 * @param pool the target pool object
 * @return status code
 **/
static inline int _init_socket(module_tcp_pool_t* pool)
{
	struct sockaddr* sockaddr;
	socklen_t sockaddr_size;
	if(!pool->conf.ipv6)
	{
		if((pool->socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create socket for TCP pipe module");

		pool->saddr.sin_family = AF_INET;
		pool->saddr.sin_addr.s_addr = inet_addr(pool->conf.bind_addr);
		pool->saddr.sin_port = htons(pool->conf.port);
		sockaddr = (struct sockaddr*)&pool->saddr;
		sockaddr_size = sizeof(struct sockaddr_in);
	}
	else
	{
		if(strcmp(pool->conf.bind_addr, "0.0.0.0") == 0)
		    pool->conf.bind_addr = "::";

		if((pool->socket_fd = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create socket for TCP pipe module");
		pool->saddr6.sin6_family = AF_INET6;
		pool->saddr6.sin6_port   = htons(pool->conf.port);
		if(inet_pton(AF_INET6, pool->conf.bind_addr, &pool->saddr6.sin6_addr) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot parse the ipv6 address %s", pool->conf.bind_addr);
		sockaddr = (struct sockaddr*)&pool->saddr6;
		sockaddr_size = sizeof(struct sockaddr_in6);
	}

	if(pool->conf.reuseaddr &&
	   setsockopt(pool->socket_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&pool->conf.reuseaddr, sizeof(pool->conf.reuseaddr)) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the reuseaddr option");

	if(_set_nonblock(pool->socket_fd) == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "Cannot set the socket FD to non-blocking mode");

	if(bind(pool->socket_fd, sockaddr, sockaddr_size) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot bind address");

	if(listen(pool->socket_fd, pool->conf.tcp_backlog) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot listen TCP port %"PRIu16, pool->conf.port);

	os_event_desc_t event = {
		.type = OS_EVENT_TYPE_KERNEL,
		.kernel = {
			.fd = pool->socket_fd,
			.event = OS_EVENT_KERNEL_EVENT_CONNECT,
			.data = NULL
		}
	};

	if(ERROR_CODE(int) == os_event_poll_add(pool->poll_obj, &event))
		ERROR_LOG_GOTO(ERR, "Cannot add socket FD to the poll list");

	LOG_DEBUG("TCP Socket has been initialized on %s:%"PRIu16, pool->conf.bind_addr, pool->conf.port);
	return 0;
ERR:
	if(pool->socket_fd >= 0) close(pool->socket_fd);
	return ERROR_CODE(int);
}

int module_tcp_pool_configure(module_tcp_pool_t* pool, const module_tcp_pool_configure_t* conf)
{
	if(NULL == pool || NULL == conf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	pool->loop_killed = 0;

	if(NULL != pool->conn_info.bitmask)
	    ERROR_RETURN_LOG(int, "FIXME: Reconfiguration of a connection pool is not allowed");

	if(NULL == conf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	pool->conf = *conf;

	if(_init_socket(pool) == ERROR_CODE(int)) goto ERR;

	if(_init_conn_info(pool, pool->conf.size) == ERROR_CODE(int)) goto ERR;

	return 0;
ERR:
	_finalize_conn_info(pool);
	return ERROR_CODE(int);
}

/**
 * @brief swap two element in the connection list
 * @param a the index in the *connection list* (rather than the index array) for the first connection object
 * @param b the index in the *connection list* (rather than the index array) for the second connection object
 * @param pool the connection pool instance object
 * @note this function will perform the following operations: <br/>
 *        1. Swap the connection object in the connection list <br/>
 *        2. Maintain the index array so that it reflects the index correctly
 * @return nothing
 **/
static inline void _swap(module_tcp_pool_t* pool, uint32_t a, uint32_t b)
{
	_node_t tmp = pool->conn_info.conn[a];
	pool->conn_info.conn[a] = pool->conn_info.conn[b];
	pool->conn_info.conn[b] = tmp;
	pool->conn_info.index[pool->conn_info.conn[a].id] = a;
	pool->conn_info.index[pool->conn_info.conn[b].id] = b;
}
/**
 * @brief adjustment to keep the heap property in the heap
 * @param idx the index in the *connection list*
 * @param pool the connection pool instance object
 * @return nothing
 **/
static inline void _heapify(module_tcp_pool_t* pool, uint32_t idx)
{
	for(;idx < pool->conn_info.heap_limit;)
	{
		uint32_t min_idx = idx;
		if(idx * 2 + 1 < pool->conn_info.heap_limit && pool->conn_info.conn[idx * 2 + 1].ts < pool->conn_info.conn[min_idx].ts)
		    min_idx = idx * 2 + 1;
		if(idx * 2 + 2 < pool->conn_info.heap_limit && pool->conn_info.conn[idx * 2 + 2].ts < pool->conn_info.conn[min_idx].ts)
		    min_idx = idx * 2 + 2;
		if(idx == min_idx) break;
		_swap(pool, idx, min_idx);
		idx = min_idx;
	}
}

/**
 * @brief upward adjustment for heap
 * @param idx the index in the *connectin list*
 * @param pool the connection pool instance object
 * @return nothing
 **/
static inline void _decrease(module_tcp_pool_t* pool, uint32_t idx)
{
	for(;idx > 0 && pool->conn_info.conn[(idx - 1) / 2].ts > pool->conn_info.conn[idx].ts; idx = (idx - 1) / 2)
	    _swap(pool, idx, (idx - 1) / 2);
}

//TODO: remodel this

/**
 * @brief close a connection and release the connection object in the connection list
 * @param idx the index in the *connection list*
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _connection_close(module_tcp_pool_t* pool, uint32_t idx)
{
	LOG_DEBUG("Closing connection object %"PRIu32, pool->conn_info.conn[idx].id);

	/* release the connection object */
	if(ERROR_CODE(int) == _release_connection_object(pool, idx))
	    LOG_WARNING("Cannot release the connection object, memory or FD leaking is possible");

	/* free the index it occupied */
	if(bitmask_dealloc(pool->conn_info.bitmask, pool->conn_info.conn[idx].id) == ERROR_CODE(int))
	    LOG_WARNING("Cannot deallocate the used connection object index %"PRIu32, pool->conn_info.conn[idx].id);

	/* If this index is in the range of inactive heap, remove it from the inactive heap first */
	if(idx < pool->conn_info.heap_limit)
	{
		pool->conn_info.conn[idx] = pool->conn_info.conn[--pool->conn_info.heap_limit];
		/**
		 * We actually do not swap the connection objects. Instead we override the released
		 * connection object with the list item in this segment.
		 *
		 * In logic, this is a swap, but because the released connection object is already invalidated.
		 * So that there's no need to carry the data of the released connection object around any more.
		 * Instead, we just do not care the what data actually it is in the place where the realsed connection
		 * object occupied.
		 *
		 * That is why we check if the idx is the connection occupied by the released connection (which is
		 * actually a place holder). If this is true, the data in the connection object is not defined, so
		 * we just ignore it.
		 *
		 * It's Ok to perform heapify in this case, because the place hold is out of the range of heap.
		 *
		 * Here's an example for a bug senario if we do not check this:
		 * 		Heap         [1,2,3]
		 * 		Active       []
		 * 		Inactive     [4,5]
		 * 		Index        [1,2,3,4,5]
		 *
		 * And we are going to delete 2
		 * * Step 1: delete 3 from the heap
		 *     Heap          [1,3]
		 *     Active        [3]
		 *     Inactive      [5]
		 *     Index         [1,2,2,4,5]
		 * * Step 2: delete "3" from the Inactive
		 *     Heap          [1,3]
		 *     Active        []
		 *     Index         [3,5]
		 *     Index         [1,2,3,4,5]   //Bug here, because we set index[idx], which is actually the place holder
		 *
		 * But this only happens we we do not fully swap the connection object, so we need check if the place holder is
		 * in this place
		 **/
		if(pool->conn_info.heap_limit > idx) pool->conn_info.index[pool->conn_info.conn[idx].id] = idx;
		_heapify(pool, idx);
		idx = pool->conn_info.active_start;
	}

	/* At this point, we are able to assume that the index to delete is out of the range of inactive heap,
	 * If this index is in the range of active list, remove it from the active list */
	if(idx < pool->conn_info.active_limit)
	{
		pool->conn_info.conn[idx] = pool->conn_info.conn[--pool->conn_info.active_limit];
		if(pool->conn_info.active_limit > idx) pool->conn_info.index[pool->conn_info.conn[idx].id] = idx;
		idx = pool->conn_info.wait_start;
	}

	/* Finally, we can ssume that the index is in the range of wait list, which means we can remove it direcly */
	if(idx < pool->conn_info.wait_limit)
	{
		pool->conn_info.conn[idx] = pool->conn_info.conn[--pool->conn_info.wait_limit];
		if(pool->conn_info.wait_limit > idx) pool->conn_info.index[pool->conn_info.conn[idx].id] = idx;
	}

	return 0;
}
/**
 * @brief activate means move the connection from inactive heap to wait list
 * @param idx the index in the *connection list*
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _connection_activate(module_tcp_pool_t* pool, uint32_t idx)
{
	if(idx >= pool->conn_info.heap_limit)
	{
		_print_stat(pool);
		ERROR_RETURN_LOG(int, "Invalid argument connection object index %"PRIu32" is out of the heap_limit", idx);
	}

	/** Remove it from the poll_obj's list, so that it won't trigger poll awake since then */
	if(ERROR_CODE(int) == os_event_poll_del(pool->poll_obj, pool->conn_info.conn[idx].fd, 1))
		ERROR_RETURN_LOG(int, "Cannot remove the connection object %"PRIu32" from the poll object list", pool->conn_info.conn[idx].id);

	/* Remove it from the inactive heap */
	_swap(pool, idx, --pool->conn_info.heap_limit);
	_heapify(pool, idx);

	/* Swap it to the wait list */
	_swap(pool, pool->conn_info.active_start, --pool->conn_info.active_limit);
	return 0;
}
/**
 * @brief checkout means adding the connection from the wait list to the active list
 * @param idx the index in the *connection list*
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _connection_checkout(module_tcp_pool_t* pool, uint32_t idx)
{
	if(idx < pool->conn_info.wait_start || idx >= pool->conn_info.wait_limit)
	{
		_print_stat(pool);
		ERROR_RETURN_LOG(int, "Invalid argument connection object index %"PRIu32" is out of the wait list", idx);
	}

	_swap(pool, idx, pool->conn_info.wait_start ++);
	return 0;
}
/**
 * @brief checkin means move the connection form active list to wait list
 * @param idx the index in the *connection list*
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _connection_checkin(module_tcp_pool_t* pool, uint32_t idx)
{
	if(idx < pool->conn_info.active_start || idx >= pool->conn_info.active_limit)
	{
		_print_stat(pool);
		ERROR_RETURN_LOG(int, "Invalid argument connection object index %"PRIu32" is out of the active list", idx);
	}

	_swap(pool, idx, -- pool->conn_info.active_limit);
	return 0;
}
/**
 * @brief deactivate means we move the connection from atcive list to inactive heap
 * @param idx the index in the *connection list*
 * @param now the current timestamp
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _connection_deactivate(module_tcp_pool_t* pool, uint32_t idx, time_t now)
{
	int rc = 0;

	if(idx < pool->conn_info.active_start || idx >= pool->conn_info.active_limit)
	{
		_print_stat(pool);
		ERROR_RETURN_LOG(int, "Invalid argument connection object index %"PRIu32" is out of the active list", idx);
		return ERROR_CODE(int);
	}

#if 0
	/* We do not do this any more, because we allow deactivated connection preseve state.
	 * And this won't have memory leak, because the user-space code wants to dispose it properly
	 * And if the user-space code dosen't do so, this won't help any way */
	/* Note: caller should make sure the pointer has been free'd properly */
	_conn_info.conn[idx].data = NULL;
#endif

	os_event_desc_t event = {
		.type = OS_EVENT_TYPE_KERNEL,
		.kernel = {
			.event = OS_EVENT_KERNEL_EVENT_IN,
			.fd   = pool->conn_info.conn[idx].fd,
			.data = pool->conn_info.index + pool->conn_info.conn[idx].id
		}
	};

	if(ERROR_CODE(int) == os_event_poll_add(pool->poll_obj, &event))
	{
		LOG_ERROR("Cannot add the connection to the poll list");
		rc = ERROR_CODE(int);
	}

	pool->conn_info.conn[idx].ts = now;

	_swap(pool, idx, pool->conn_info.active_start ++);

	_decrease(pool, pool->conn_info.heap_limit - 1);

	return rc;
}

__attribute__((used))
/**
 * @brief get the address of the peer
 * @param sockfd the socket fd
 * @param ipv6 indicates if this can be a ipv6 address, this is used when the IPV6 support is on
 * @param buffer the buffer used to contain the converted result. Because the module may have multiple instance, so we
 *        Can not use the the static variable safely. Instead we use should use the buffer allocated in the pool context.
 * @return the peer name
 **/
static inline const char* _get_peer_name(int sockfd, int ipv6, char* buffer)
{
	union{
		struct sockaddr_in ipv4;
		struct sockaddr_in6 ipv6;
	} addr;
	socklen_t addr_size = ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

	if(getpeername(sockfd, (struct sockaddr*)&addr, &addr_size) < 0)
	{
		LOG_WARNING_ERRNO("cannot figure out the name of the peer");
		return "unknown";
	}

	const char* result = NULL;

	if(ipv6)
	    result = inet_ntop(AF_INET6, &addr.ipv6.sin6_addr, buffer, INET6_ADDRSTRLEN);
	else
	    result = inet_ntop(AF_INET, &addr.ipv4.sin_addr, buffer, INET_ADDRSTRLEN);

	if(NULL == result)
	{
		LOG_WARNING_ERRNO("invalid address");
		return "unknown";
	}

	return result;
}

/**
 * @brief accept a request from listening socket
 * @param pool the connection pool instance
 * @param now the current time stamp
 * @return status code
 **/
static inline int _accpet_request(module_tcp_pool_t* pool, time_t now)
{
	int data_fd;

	if(bitmask_full(pool->conn_info.bitmask) != 0)
	{
		LOG_INFO("Connection pool is full, let the incoming request wait");
		return -1;
	}

	socklen_t addr_len = sizeof(struct sockaddr_in);
	for(;-1 != (data_fd = accept(pool->socket_fd, (struct sockaddr*)&pool->saddr, &addr_len));)
	{
		uint32_t id = (uint32_t)bitmask_alloc(pool->conn_info.bitmask);
		if(ERROR_CODE(uint32_t) == id)
		{
			LOG_WARNING("cannot allocate new ID to the incoming request");
			goto ERR;
		}

		if(_set_nonblock(data_fd) == ERROR_CODE(int))
		{
			LOG_WARNING("cannot set incoming FD to nonblocking mode");
			goto ERR;
		}

		/* Make a new connection object */
		pool->conn_info.conn[pool->conn_info.nconnections].ts = now;
		pool->conn_info.conn[pool->conn_info.nconnections].fd = data_fd;
		pool->conn_info.conn[pool->conn_info.nconnections].id = id;
		pool->conn_info.conn[pool->conn_info.nconnections].data = NULL;
		pool->conn_info.index[id] = pool->conn_info.nconnections;
		pool->conn_info.wait_limit ++;

		/* The new incoming request should not be in waiting list, because it may connect but no data
		 * The sane way to handle this is adding it to heap and let next poll wake it up */
		_swap(pool, pool->conn_info.wait_limit - 1, pool->conn_info.wait_start ++);
		_swap(pool, pool->conn_info.active_limit - 1, pool->conn_info.active_start ++);
		_decrease(pool, pool->conn_info.heap_limit - 1);

		/* Because it should be in the heap, so add it to poll queue */
		os_event_desc_t event = {
			.type = OS_EVENT_TYPE_KERNEL,
			.kernel = {
				.fd = data_fd,
				.event = OS_EVENT_KERNEL_EVENT_IN,
				.data = pool->conn_info.index + id
			}
		};

		if(ERROR_CODE(int) == os_event_poll_add(pool->poll_obj, &event))
		{
			LOG_ERROR("Could not register the new connection to the event list");
			continue;
		}

		LOG_INFO("accepted new connection from %s as connection object %"PRIu32, _get_peer_name(data_fd, pool->conf.ipv6, pool->addr_str_buf), id);

		continue;
ERR:
		if(id != ERROR_CODE(uint32_t)) bitmask_dealloc(pool->conn_info.bitmask, id);
		if(data_fd >= 0) close(data_fd);
	}
	if(errno != EAGAIN && errno != EWOULDBLOCK)
	    LOG_ERROR("unexpected error code: %s", strerror(errno));
	return 0;
}
/**
 * @brief print the debug information about the queue message in the log
 * @param msg the queue message
 * @param cont indicates if this is a continuation message
 * @param idx  if this is not a continuation message, the index in the message queue
 * @param pool the connection pool instance object
 * @return nothing
 **/
static inline void _print_queue_message(module_tcp_pool_t* pool, const _queue_message_t* msg, int cont, uint64_t idx)
#ifdef LOG_DEBUG_ENABLED
{
	uint32_t pos = pool->conn_info.index[msg->id];
	const char* msg_name = "invalid-queue-message";
	switch(msg->type)
	{
		case _QM_DEACTIVATE: msg_name = "deactivate-connection"; break;
		case _QM_CHECKIN:    msg_name = "check-in-connection"; break;
		case _QM_CLOSE:      msg_name = "close-connection"; break;
		default:
		    LOG_WARNING("Invalid message type");
	}
	if(cont)
	    LOG_DEBUG("Continuation QM: operation = \"%s\", target-id = %"PRIu32", target-pos = %"PRIu32, msg_name, msg->id, pos);
	else
	    LOG_DEBUG("QM #%"PRIu64": operation = \"%s\", target-id = %"PRIu32", target-pos = %"PRIu32, idx, msg_name, msg->id, pos);
}
#else
{
	(void) pool;
	(void) msg;
	(void) idx;
	(void) cont;
}
#endif
/**
 * @brief process a queue message
 * @param msg the queue message
 * @param now the current timestamp
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _process_queue_message(module_tcp_pool_t* pool, const _queue_message_t* msg, time_t now)
{
	int rc = -1;
	uint32_t id = pool->conn_info.index[msg->id];
	switch(msg->type)
	{
		case _QM_DEACTIVATE:
		    pool->conn_info.conn[id].data = msg->data;
		    rc = _connection_deactivate(pool, id, now);
		    break;
		case _QM_CLOSE:
		    rc = _connection_close(pool, id);
		    break;
		case _QM_CHECKIN:
		    pool->conn_info.conn[id].data = msg->data;
		    rc = _connection_checkin(pool, id);
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Unknown message type, code bug!");
	}

	return rc;
}

/**
 * @brief this function perform operations based on the information provided by the event fd
 * @param now the timestamp
 * @param pool the connection pool instance object
 * @return the status code
 **/
static inline int _queue_message_exec(module_tcp_pool_t* pool, time_t now)
{

	LOG_DEBUG("Performing queue message operations");

	if(ERROR_CODE(int) == os_event_user_event_consume(pool->poll_obj, pool->event_fd))
		ERROR_RETURN_LOG(int, "Cannot consume user event");

	uint32_t limit = pool->conn_info.q_rear;
	BARRIER();
	for(; pool->conn_info.q_front != limit;)
	{
		uint64_t current = pool->conn_info.q_front;
		const _queue_message_t* msg = &pool->conn_info.queue[current & pool->conn_info.q_mask];

		_print_queue_message(pool, msg, 0, current);

		if(_process_queue_message(pool, msg, now) == ERROR_CODE(int))
		    ERROR_RETURN_LOG(int, "Cannot perform the queue message operation");

		BARRIER();
		arch_atomic_sw_increment_u32(&pool->conn_info.q_front);
	}

	return 0;
}
/**
 * @brief this function poll the event, and check out all the
 *        active connections and move them at the end of the heap
 **/
static inline int _poll_event(module_tcp_pool_t* pool)
{
	/* Determine the max time for this poll call to wait */
	time_t   now = time(NULL);
	time_t   time_to_sleep = 0;
	if(pool->conn_info.heap_limit > 0)
	{
		time_to_sleep = pool->conf.min_timeout;
		if(pool->conn_info.conn[0].ts + pool->conf.ttl >= now + time_to_sleep)
		    time_to_sleep = pool->conn_info.conn[0].ts + pool->conf.ttl - now;
	}

	int timeout = (time_to_sleep > 0) ? (int)time_to_sleep * 1000 : -1;
	int i, incoming = 0;

	if(timeout > 0)
	    LOG_DEBUG("waiting for socket events for up to %d ms", timeout);
	else
	    LOG_DEBUG("waiting for socket event");

	int result = os_event_poll_wait(pool->poll_obj, pool->conf.event_size, timeout);

	if(result == ERROR_CODE(int))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot poll event");
	else
	{
		for(i = 0; i < result; i ++)
		{
			void* data = os_event_poll_take_result(pool->poll_obj, (size_t)i);

			/* If the fd indicates we have queue message is pending */
			if(&pool->event_fd == data) 
			{
				if(_queue_message_exec(pool, now) == ERROR_CODE(int))
				{
					LOG_WARNING("Cannot execute the queue message");
					continue;
				}
			}
			/* If this is a connection FD */
			else if(NULL != data)
			{
				uint32_t idx = *(uint32_t*)data;
				if(_connection_activate(pool, idx) < 0)
				{
					LOG_WARNING("cannot activate the connection");
					continue;
				}
			}
			/* If this is the listening FD */
			else incoming = 1;
		}
	}

	/* kick the timeout client out */
	for(;pool->conn_info.heap_limit > 0 && pool->conn_info.conn[0].ts + pool->conf.ttl <= now; _connection_close(pool, 0))
	    LOG_DEBUG("closing timed out connection %d", pool->conn_info.conn[0].fd);

	/* Process incoming request */
	if(incoming)
	{
		if(_accpet_request(pool, now) == ERROR_CODE(int))
		{
			LOG_ERROR("Cannot accept incoming request");
		}
		else
		{
			LOG_DEBUG("Incoming request accepted");
		}
	}

	LOG_DEBUG("Poll finished, current pool layout:");
	_print_stat(pool);
	return 0;
}
int module_tcp_pool_poll_event(module_tcp_pool_t* pool)
{
	return _poll_event(pool);
}
int module_tcp_pool_connection_get(module_tcp_pool_t* pool, module_tcp_pool_conninfo_t* buf)
{
	if(NULL == buf || NULL == pool) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc;
	for(;!pool->loop_killed && pool->conn_info.wait_start == pool->conn_info.wait_limit && (rc = _poll_event(pool)) != ERROR_CODE(int););

	if(pool->loop_killed)
	{
		LOG_INFO("thread gets killed!");
		return ERROR_CODE(int);
	}

	_node_t* connection = pool->conn_info.conn + pool->conn_info.wait_limit - 1;

	buf->fd   = connection->fd;
	buf->data = connection->data;
	buf->idx  = connection->id;

	connection->data = NULL;

	if(_connection_checkout(pool, pool->conn_info.wait_limit - 1) < 0)
	{
		LOG_ERROR("cannot checkout connection");
		return ERROR_CODE(int);
	}
	LOG_DEBUG("connection object %"PRIu32" is returned as an active connection", connection->id);
	_print_stat(pool);

	return 0;
}
/**
 * @brief initialize a queue message
 * @param msg the target message buffer
 * @param id the connection object id
 * @param data the user-space data needs to be attached
 * @param mode the release mode swtich
 * @param pool the connection pool instance object
 * @return status code
 **/
static inline int _init_connection_release_message(module_tcp_pool_t* pool, _queue_message_t* msg, uint32_t id, void* data, int mode)
{
	(void) pool;

	if(mode == MODULE_TCP_POOL_RELEASE_MODE_AUTO)
	{
		if(NULL == data) mode = MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_DATA;
		else mode = MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_READ;
	}

	switch(mode)
	{
		case MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_DATA:
		    LOG_DEBUG("QM#%"PRIu32": deactivate the connection object %"PRIu32" from active list to inactive heap", pool->conn_info.q_rear, id);
		    msg->type = _QM_DEACTIVATE;
		    msg->id   = id;
		    msg->data = data;
		    return 0;
		case MODULE_TCP_POOL_RELEASE_MODE_PURGE:
		    LOG_DEBUG("QM#%"PRIu32": close the connection object %"PRIu32" as required", pool->conn_info.q_rear, id);
		    msg->type = _QM_CLOSE;
		    msg->id   = id;
		    return 0;
		case MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_READ:
		    LOG_DEBUG("QM#%"PRIu32": check in the connection object %"PRIu32" from active list to wait list", pool->conn_info.q_rear, id);
		    msg->type = _QM_CHECKIN;
		    msg->id   = id;
		    msg->data = data;
		    return 0;
		default:
		    ERROR_RETURN_LOG(int, "Invalid mode swtich %d", mode);
	}
}

int module_tcp_pool_connection_release(module_tcp_pool_t* pool, uint32_t id, void* data, int mode)
{
	if(NULL == pool) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(pthread_mutex_lock(&pool->conn_info.q_mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot acquire the global message queue mutex");

	if(_init_connection_release_message(pool, pool->conn_info.queue + (pool->conn_info.q_rear & pool->conn_info.q_mask), id, data, mode) == ERROR_CODE(int))
	{
		if(pthread_mutex_unlock(&pool->conn_info.q_mutex) < 0)
		    LOG_ERROR_ERRNO("Cannot release the global message queue mutex");
		ERROR_RETURN_LOG(int, "Cannot initialize the connection release message");
	}

	BARRIER();

	arch_atomic_sw_increment_u32(&pool->conn_info.q_rear);

	if(pthread_mutex_unlock(&pool->conn_info.q_mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot release the global message queue mutex");

	uint64_t val = 1;
	if(write(pool->event_fd, &val, sizeof(val)) > 0)
	{
		LOG_DEBUG("connection release queue message posted");
		return 0;
	}
	else
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot write to the event fd");
}

void module_tcp_pool_loop_killed(module_tcp_pool_t* pool)
{
	pool->loop_killed = 1;
}

