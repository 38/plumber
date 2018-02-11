/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <pstd.h>
#include <connection.h>

/**
 * @brief The node in the hash table that is used to describe the peer
 **/
typedef struct _peer_t _peer_t;

/**
 * @brief The data structure used to tracking an unused connection
 **/
typedef struct _conn_t{
	struct _conn_t* next_conn;   /*!< The next node in connection list */
	struct _conn_t* prev_conn;   /*!< The prev node in connection list */
	_peer_t*        peer;        /*!< The peer node */
	int             fd;          /*!< The FD for this socket */
	struct _conn_t* lru_next;    /*!< The next node least recent used list */
} _conn_t;

/**
 * @brief The actual data structure used to keep tracking the data of a peer 
 **/
struct _peer_t {
	uint32_t    count;       /*!< The numer of connections in the list */
	uint32_t    port;        /*!< The port id */
	char*       domain_name; /*!< The domain of the peer */
	uint64_t    hash[2];     /*!< The 128 bit hash code */
	_conn_t*    conn_list;   /*!< The connection list */
	struct _peer_t* hash_next; /*!< The next node in the hash table */
};

/**
 * @brief The thread local, each of the thread owns its connection pool
 **/
typedef struct {
	uint32_t   num_conn;     /*!< The number of connections in the pool */
	_conn_t*   lru_list;     /*!< The least recently used list */
	int        epoll;        /*!< The epoll that used to monitor the socket close event */
	uintpad_t  __padding__[0];
	_peer_t*   table[0];        /*!< The hash table used for the peer */
} _thread_conn_pool_t;

/**
 * @brief The data for the pool structure
 **/
static struct {
	uint32_t             init_count;    /*!< How many servlets are using the connection pool */
	uint32_t             pool_size;     /*!< The connection pool size */
	uint32_t             peer_limit;    /*!< How many connection for the same peer */
	uint32_t             hash_size;     /*!< The number of slots in the hash table */
	pstd_thread_local_t* thread_data;   /*!< The thread local pool for each thread */
} _pool;

static inline uint32_t _get_hash_size(void)
{
	/* TODO: make this configurable */
	return 4073;
}

static void* _thread_init(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;
	_thread_conn_pool_t* ret = (_thread_conn_pool_t*)calloc(sizeof(_pool) + sizeof(_peer_t) * _pool.hash_size, 1);

	if(NULL == ret) 
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the thread local");

	if((ret->epoll = epoll_create1(0)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create epoll for socket closure detection");

	return ret;

ERR:
	free(ret);
	return NULL;

}

static int _thread_finalize(void* local, const void* data)
{
	int rc = 0;
	(void)data;

	_thread_conn_pool_t* pool = (_thread_conn_pool_t*)local;

	_conn_t* ptr;

	for(ptr = pool->lru_list; ptr != NULL;)
	{
		_conn_t* this = ptr;
		ptr = ptr->lru_next;
		
		if(this->fd >= 0 && close(this->fd) < 0)
		{
			LOG_ERROR_ERRNO("Cannot close the FD %d", this->fd);
			rc = ERROR_CODE(int);
		}

		free(this);
	}

	if(pool->epoll >= 0 && close(pool->epoll) < 0)
	{
		LOG_ERROR_ERRNO("Cannot calose the epoll object");
		rc = ERROR_CODE(int);
	}

	free(pool);

	return rc;
}

int connection_pool_init(uint32_t size, uint32_t peer_pool_size)
{
	if(_pool.pool_size < size) 
		_pool.pool_size = size;

	if(_pool.peer_limit < peer_pool_size) 
		_pool.peer_limit = peer_pool_size;

	if(_pool.init_count == 0)
	{
		_pool.hash_size = _get_hash_size();

		if(NULL == (_pool.thread_data = pstd_thread_local_new(_thread_init, _thread_finalize, NULL)))
			ERROR_RETURN_LOG(int, "Cannot allocate thread local for the connection pool");
	}

	_pool.init_count ++;

	return 0;
}

int connection_pool_finalize(void)
{
	if(_pool.init_count == 0) return 0;

	if(0 == --_pool.init_count && ERROR_CODE(int) == pstd_thread_local_free(_pool.thread_data))
		ERROR_RETURN_LOG(int, "Cannot dispose the thread local connection pool");

	return 0;
}
