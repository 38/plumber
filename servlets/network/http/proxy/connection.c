/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>

#include <utils/hash/murmurhash3.h>

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
	struct _conn_t* conn_next;   /*!< The next node in connection list */
	struct _conn_t* conn_prev;   /*!< The prev node in connection list */
	struct _conn_t* lru_next;    /*!< The next node least recent used list */
	struct _conn_t* lru_prev;    /*!< The previous node in the LRU list */
	_peer_t*        peer;        /*!< The peer node */
	int             fd;          /*!< The FD for this socket */
} _conn_t;

/**
 * @brief The actual data structure used to keep tracking the data of a peer
 **/
struct _peer_t {
	uint32_t            count;       /*!< The numer of connections in the list */
	uint32_t            port;        /*!< The port id */
	char*               domain_name; /*!< The domain of the peer */
	uint64_t            hash[2];     /*!< The 128 bit hash code */
	_conn_t*            conn_list;   /*!< The connection list */
	struct _peer_t*     peer_next;   /*!< The next node in the hash table */
};

/**
 * @brief The data for the pool structure
 **/
static struct {
	uint32_t             num_conn;     /*!< The number of connections in the pool */
	_conn_t*             lru_begin;    /*!< The least recently used list */
	_conn_t*             lru_end;      /*!< The last item in LRU list */
	_peer_t**            table;        /*!< The hash table used for the peer */
	uint32_t             init_count;   /*!< How many servlets are using the connection pool */
	uint32_t             pool_size;    /*!< The connection pool size */
	uint32_t             peer_limit;   /*!< How many connection for the same peer */
	uint32_t             hash_size;    /*!< The number of slots in the hash table */
	pthread_mutex_t      mutex;        /*!< The global connection pool mutex */
} _pool;

static inline uint32_t _get_hash_size(void)
{
	/* TODO: make this configurable */
	return 4073;
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

		if(NULL == (_pool.table = (_peer_t**)calloc(sizeof(_peer_t*), _pool.hash_size)))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the hash table");

		if((errno = pthread_mutex_init(&_pool.mutex, NULL)) != 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the conneciton pool mutex");

		goto INIT_DONE;
ERR:
		if(NULL != _pool.table) free(_pool.table);
		return ERROR_CODE(int);
	}
INIT_DONE:

	_pool.init_count ++;

	return 0;
}

int connection_pool_finalize(void)
{
	int rc = 0;
	if(_pool.init_count == 0) return 0;

	if(0 == --_pool.init_count)
	{
		_conn_t* ptr;

		for(ptr = _pool.lru_begin; ptr != NULL;)
		{
			_conn_t* this = ptr;
			ptr = ptr->lru_next;

			if(this->fd >= 0 && close(this->fd) < 0)
			{
				LOG_ERROR_ERRNO("Cannot close the FD %d", this->fd);
				rc = ERROR_CODE(int);
			}

			if(ERROR_CODE(int) == pstd_mempool_free(this))
			{
				LOG_ERROR("Cannot dispose the peer object");
				rc = ERROR_CODE(int);
			}
		}

		if(NULL != _pool.table)
		{
			uint32_t i;
			for(i = 0; i < _pool.hash_size; i ++)
			{
				_peer_t* p_ptr;
				for(p_ptr = _pool.table[i]; p_ptr != NULL;)
				{
					_peer_t* this = p_ptr;
					p_ptr = p_ptr->peer_next;
					free(this->domain_name);
					if(ERROR_CODE(int) == pstd_mempool_free(this))
					{
						LOG_ERROR("Cannot dispose the peer object");
						rc = ERROR_CODE(int);
					}
				}
			}
			free(_pool.table);
		}

		if((errno = pthread_mutex_destroy(&_pool.mutex)) != 0)
		{
			LOG_ERROR_ERRNO("Cannot destroy the mutex");
			rc = ERROR_CODE(int);
		}
	}
	return rc;
}

static inline void _lru_remove(_conn_t* conn)
{
	if(conn->lru_prev == NULL)
	    _pool.lru_begin = conn->lru_next;
	else
	    conn->lru_prev->lru_next = conn->lru_next;

	if(conn->lru_next == NULL)
	    _pool.lru_end = conn->lru_prev;
	else
	    conn->lru_next->lru_prev = conn->lru_prev;
}

static inline void _lru_add(_conn_t* conn)
{
	conn->lru_next = _pool.lru_begin;
	conn->lru_prev = NULL;
	_pool.lru_begin = conn;
	if(_pool.lru_end == NULL)
	    _pool.lru_end = conn;
}

static inline void _hash(uint32_t port, const char* domain_name, size_t domain_len, uint64_t* out)
{
	murmurhash3_128(domain_name, domain_len, port * 0x3f27145au, out);
}

static inline uint32_t _hash_slot(const uint64_t* hash, uint32_t hash_size)
{
	uint32_t slot = (uint32_t)((1ull<<32) % hash_size);
	slot = (uint32_t)(((uint64_t)slot * (uint64_t)slot) % hash_size);
	slot = (uint32_t)((slot * hash[1] + hash[0]) % hash_size);
	return slot;
}

static inline int _peer_match(const _peer_t* peer, uint32_t port, const char* domain_name, size_t domain_len, const uint64_t* hash)
{
	if(peer->hash[0] != hash[0] || peer->hash[1] != hash[1])
	    return 0;
#ifdef NETWORK_HTTP_PROXY_STRICT_KEY_COMP
	if(strncmp(peer->domain_name, domain_name, domain_len) != 0)
	    return 0;
	if(port != peer->port)
	    return 0;
#else
	(void)port;
	(void)domain_name;
	(void)domain_len;
#endif
	return 1;
}

static inline void _release_connection(_conn_t* conn)
{
	_lru_remove(conn);

	_peer_t* peer = conn->peer;

	if(conn->conn_prev == NULL)
	    peer->conn_list = conn->conn_next;
	else
	    conn->conn_prev->conn_next = conn->conn_next;

	if(conn->conn_next != NULL)
	    conn->conn_next->conn_prev = conn->conn_prev;

	peer->count --;
	_pool.num_conn --;

	if(close(conn->fd) < 0)
	    LOG_WARNING_ERRNO("Cannot close the FD %d", conn->fd);

	if(ERROR_CODE(int) == pstd_mempool_free(conn))
	    LOG_WARNING_ERRNO("Cannot dispose the connection object");
}

static inline int _conn_add(uint32_t port, const char* domain_name, size_t domain_len, int fd)
{
	uint64_t hash[2];
	_hash(port, domain_name, domain_len, hash);
	uint32_t slot = _hash_slot(hash, _pool.hash_size);

	_peer_t* peer;
	_conn_t* conn;

	for(peer = _pool.table[slot]; NULL != peer && !_peer_match(peer, port, domain_name, domain_len, hash) ; peer = peer->peer_next);

	if(peer == NULL)
	{
		if(NULL == (peer = pstd_mempool_alloc(sizeof(_peer_t))))
		    ERROR_RETURN_LOG(int, "Cannot allocate memory for the peer node");
		peer->count = 0;

		peer->port = port;
		if(NULL == (peer->domain_name = malloc(domain_len + 1)))
		    ERROR_LOG_ERRNO_GOTO(ALLOC_ERR, "Cannot allocate memory for the domain name");
		memcpy(peer->domain_name, domain_name, domain_len);
		peer->domain_name[domain_len] = 0;

		peer->hash[0] = hash[0];
		peer->hash[1] = hash[1];

		peer->conn_list = NULL;

		peer->peer_next = _pool.table[slot];
		_pool.table[slot] = peer;
		goto ADD_CONN;
ALLOC_ERR:
		pstd_mempool_free(peer);
		return ERROR_CODE(int);
	}
ADD_CONN:

	/* Step 1: We need to kickout some connections from the peer list if needed */
	while(peer->count >= _pool.peer_limit)
	    _release_connection(peer->conn_list);

	/* Step 2: We need to kickout the LRU list */
	while(_pool.pool_size <= _pool.num_conn)
	    _release_connection(_pool.lru_end);

	if(NULL == (conn = pstd_mempool_alloc(sizeof(_conn_t))))
	    ERROR_RETURN_LOG(int, "Cannot allocate memory for the new connection");

	conn->fd = fd;
	conn->peer = peer;

	conn->conn_next = peer->conn_list;
	conn->conn_prev = NULL;
	peer->conn_list = conn;

	_lru_add(conn);

	peer->count ++;
	_pool.num_conn ++;

	return 0;
}

static inline int _conn_get(const char* domain_name, size_t domain_len, uint32_t port)
{
	uint64_t hash[2];
	_hash(port, domain_name, domain_len, hash);
	uint32_t slot = _hash_slot(hash, _pool.hash_size);

	_peer_t* peer;

	for(peer = _pool.table[slot]; NULL != peer && !_peer_match(peer, port, domain_name, domain_len, hash) ; peer = peer->peer_next);

	if(NULL == peer || peer->conn_list == NULL)
	    return -1;

	_conn_t* this = peer->conn_list;

	peer->conn_list = this->conn_next;

	if(peer->conn_list != NULL)
	    peer->conn_list->conn_prev = NULL;

	_pool.num_conn --;
	peer->count --;

	int ret = this->fd;

	_lru_remove(this);

	if(ERROR_CODE(int) == pstd_mempool_free(this))
	    LOG_WARNING("Cannot dispose the connection");

	return ret;
}

int connection_pool_checkout(const char* hostname, size_t hostname_len, uint16_t port, int* fd)
{
	if(NULL == hostname || hostname_len == 0 || NULL == fd)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if((errno = pthread_mutex_lock(&_pool.mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot lock the connection pool mutex");

	*fd = _conn_get(hostname, hostname_len, port);

	if((errno = pthread_mutex_unlock(&_pool.mutex)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot unlock the connection pool mutex");

	return (*fd >= 0);
ERR:
	if(*fd >= 0) close(*fd);
	return ERROR_CODE(int);
}

int connection_pool_checkin(const char* hostname, size_t hostname_len, uint16_t port, int fd)
{
	int rc = 0;
	if(NULL == hostname || hostname_len == 0 || fd <= 0)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if((errno = pthread_mutex_lock(&_pool.mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot lock the connection pool mutex");

	if(ERROR_CODE(int) == _conn_add(port, hostname, hostname_len, fd))
	    rc = ERROR_CODE(int);

	if((errno = pthread_mutex_unlock(&_pool.mutex)) != 0)
	    LOG_WARNING_ERRNO("Cannot relaese the pool mutex");

	if(ERROR_CODE(int) == rc && fd >= 0 && close(fd) < 0)
	    LOG_WARNING_ERRNO("Cannot close the fd");

	return rc;
}
