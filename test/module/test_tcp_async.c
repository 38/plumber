/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <constants.h>
#include <stdint.h>
#include <stdlib.h>
#include <testenv.h>
#include <module/tcp/async.h>
#include <pthread.h>
#ifdef __LINUX__
#include <sys/eventfd.h>
#endif
#include <unistd.h>
#include <errno.h>
/**
 * @brief a mocked TCP connection
 **/
typedef struct {
	int efd;            /*!< the event fd */
#ifndef __LINUX__ 
	int pipe[2];         /*!< The EFD pipes */
#endif
	int busy;           /*!< if this socket is currently busy */
	int block;          /*!< indicates if the write should block the async thread */
	int mocked_err;     /*!< force the write function return an mocked error */
	ssize_t bytes_to_accept; /*!< indicates how many bytes we want the socket to accept this time */
	pthread_mutex_t mutex;   /*!< the mutex used to synchronize the async loop and the main thread */
	pthread_cond_t  cond;     /*!< the conditional variable used for sync */
	size_t buffer_used;       /*!< the bytes in buffer has been used */
	char buf[4096];           /*!< the data buffer */
} mocked_connection_t;

/**
 * @brief a mocked data handler
 **/
typedef struct {
	uint32_t stage;    /*!< in which state the data handler is */
	int error;    /*!< indicates if the connection object ends up with a error status (&lt;0) */
	int disposed; /*!< indicates if the connection has been disposed */
	uint32_t blocks; /*!< the bit incidates what function we should block */
} data_handle_t;

mocked_connection_t conn[128];
data_handle_t dh[sizeof(conn) / sizeof(*conn)];

module_tcp_async_loop_t* loop;

/** @brief the synchronization mutex */
pthread_mutex_t sync_mutex;
/** @brief the syhchronization condition variable */
pthread_cond_t sync_cond;
/** @brief the sync flags */
int sync_flag = -1;
enum {
	AS_WRITE = 1,
	AS_GETDATA = 2,
	AS_ERROR   = 4,
	AS_DISPOSE = 8,
	AS_WR_FIN  = 16
};

uint32_t conn_id = ERROR_CODE(uint32_t);

void _set_connction_busy(uint32_t cid, int busy)
{
	if(busy) conn[cid].busy = 1;
	else
	{
		conn[cid].busy = 0;
#ifdef __LINUX__
		eventfd_write(conn[cid].efd, 1);
#else
		uint64_t val = 1;
		write(conn[cid].pipe[1], &val, sizeof(val));
#endif
	}
}

void _set_block_bits(uint32_t cid, uint32_t blocks)
{
	dh[cid].blocks = blocks;
}

void _notify_main_thread(int func)
{
	pthread_mutex_lock(&sync_mutex);
	sync_flag = func;
	pthread_cond_signal(&sync_cond);
	pthread_mutex_unlock(&sync_mutex);
}
void _wait_main_thread(mocked_connection_t* c)
{
	LOG_DEBUG("Wait for the main thread unblock");
	pthread_mutex_lock(&c->mutex);
	while(c->block)
	    pthread_cond_wait(&c->cond, &c->mutex);
	c->block = 1;
	pthread_mutex_unlock(&c->mutex);
	LOG_DEBUG("Async thread unblocked");
}
ssize_t test_write(int fd, const void* data, size_t sz)
{
	LOG_DEBUG("test_write begin");
	for(conn_id = 0; conn_id < sizeof(conn) / sizeof(*conn);  conn_id ++)
	    if(conn[conn_id].efd == fd) break;
	if(conn_id == sizeof(conn) / sizeof(*conn))
	{
		conn_id = ERROR_CODE(uint32_t);
		errno = EINVAL;
		return -1;
	}

	mocked_connection_t* c = conn + conn_id;
	int busy = c->busy;
	int mocked_err = c->mocked_err;
	_notify_main_thread(AS_WRITE);
	if(dh[conn_id].blocks & AS_WRITE) _wait_main_thread(c);

	if(busy)
	{
		LOG_DEBUG("test_write busy");
		conn_id = ERROR_CODE(uint32_t);
		errno = EAGAIN;

		return -1;
	}
	if(mocked_err)
	{
		LOG_DEBUG("test_write error");
		conn_id = ERROR_CODE(uint32_t);
		errno = c->mocked_err;
		return -1;
	}


	ssize_t ret = (ssize_t)sz;

	if(ret > c->bytes_to_accept) ret = c->bytes_to_accept;

	memcpy(c->buf, data, (size_t)ret);


	LOG_DEBUG("test_write succeeded");
	_notify_main_thread(AS_WR_FIN);
	if(dh[conn_id].blocks & AS_WR_FIN) _wait_main_thread(c);
	conn_id = ERROR_CODE(uint32_t);
	return ret;
}

size_t _get_data_1(uint32_t id, void* buffer, size_t size, module_tcp_async_loop_t* loop)
{
	(void)loop;
	conn_id = id;

	data_handle_t* h = (data_handle_t*)module_tcp_async_get_data_handle(loop, conn_id);

	if(h != dh + conn_id)
	    ERROR_RETURN_LOG(size_t, "unexpected data handler!");

	_notify_main_thread(AS_GETDATA);
	if(dh[conn_id].blocks & AS_GETDATA) _wait_main_thread(conn + conn_id);

	uint32_t cur = (h->stage);

	LOG_DEBUG("data function called, current state: %u", cur);

	if(cur%2 == 1) return 0; //data is not ready

	if(size < sizeof(cur)) ERROR_RETURN_LOG(size_t, "invalid read size %zu", size);

	*(uint32_t*)buffer = cur;

	return sizeof(cur);
}

int _error_handler_1(uint32_t id, module_tcp_async_loop_t* loop)
{
	(void)loop;
	conn_id = id;

	data_handle_t* h = (data_handle_t*)module_tcp_async_get_data_handle(loop, conn_id);

	if(h != dh + conn_id)
	    ERROR_RETURN_LOG(int, "unexpected data handler!");

	_notify_main_thread(AS_ERROR);
	if(dh[conn_id].blocks & AS_ERROR) _wait_main_thread(conn + conn_id);

	h->error = 1;

	LOG_DEBUG("error status has been set for connection #%u", conn_id);
	return 0;
}

int _dispose_handler_1(uint32_t id, module_tcp_async_loop_t* loop)
{
	(void)loop;
	conn_id = id;
	data_handle_t* h = (data_handle_t*)module_tcp_async_get_data_handle(loop, conn_id);

	if(h != dh + conn_id)
	    ERROR_RETURN_LOG(int, "unexpected data handler!");

	h->disposed = 1;

	LOG_DEBUG("connection %u has been released", conn_id);
	_notify_main_thread(AS_DISPOSE);
	if(dh[conn_id].blocks & AS_DISPOSE) _wait_main_thread(conn + conn_id);
	return 0;
}

static inline void _wait_async_thread(int func)
{
	LOG_DEBUG("main thread: waiting for the async thread gets ready");
	pthread_mutex_lock(&sync_mutex);
	while(sync_flag != func)
	    pthread_cond_wait(&sync_cond, &sync_mutex);
	sync_flag = -1;
	pthread_mutex_unlock(&sync_mutex);
	LOG_DEBUG("main thread: async thread gets ready");
}

static inline void _unblock_async_thread(uint32_t cid)
{
	pthread_mutex_lock(&conn[cid].mutex);
	conn[cid].block = 0;
	pthread_cond_signal(&conn[cid].cond);
	pthread_mutex_unlock(&conn[cid].mutex);
}

int create_loop()
{
	ASSERT_PTR(loop = module_tcp_async_loop_new(128, 32, 240, test_write), CLEANUP_NOP);
	return 0;
}

int cleanup_loop()
{
	ASSERT_OK(module_tcp_async_loop_free(loop), CLEANUP_NOP);

	return 0;
}
__attribute__((used)) static inline uint8_t _as_u8(void* start, int n)
{
	return ((uint8_t*)start)[n];
}
__attribute__((used)) static inline uint16_t _as_u16(void* start, int n)
{
	return ((uint16_t*)start)[n];
}
__attribute__((used)) static inline uint32_t _as_u32(void* start, int n)
{
	return ((uint32_t*)start)[n];
}
int single_async_write()
{
	uint32_t data;
	/* Because the initial data state is wait, so nothing should happen here, so we can block every thing at this point */
	_set_block_bits(0, 0xffffffffu);
	ASSERT_OK(module_tcp_async_write_register(loop, 0, conn[0].efd, 16, _get_data_1, _dispose_handler_1, _error_handler_1, dh + 0), CLEANUP_NOP);
	/* C:R D:W */
	ASSERT_OK(module_tcp_async_write_data_ready(loop, 0), CLEANUP_NOP);
	/* after we send this, the get data function should be called first */
	_wait_async_thread(AS_GETDATA);
	dh[0].stage = data = 0x890abcde; /* feed some data and make the data state becomes ready */
	_unblock_async_thread(0);

	/* C:R, D:R */
	/* then it should have the first write attempt */
	_wait_async_thread(AS_WRITE);
	_unblock_async_thread(0);
	/* because the connections are set to busy when it's created, so the result should be connection becomes busy */
	/* C:W, D:R */
	dh[0].stage = 1;  /* make the data source not ready */
	/* nothing could happen at this point */
	usleep(1000);   /* do a sleep if there's anything unexpected happen */
	_set_connction_busy(0, 0);  /* make the connection ready */
	_wait_async_thread(AS_GETDATA); /* at this point, the connection becomes ready again, so it will access getdata */
	_unblock_async_thread(0);

	_wait_async_thread(AS_WRITE);  /* another write attempt */
	conn[0].bytes_to_accept = 2;   /* we write only 2 bytes to the fd */
	_unblock_async_thread(0);
	_wait_async_thread(AS_WR_FIN); /* it should perfomed the actual write at this point (2 bytes) */
	ASSERT(_as_u16(&data, 0) == _as_u16(conn[0].buf, 0), CLEANUP_NOP);  /* make sure the data is expected */
	_unblock_async_thread(0);
	/* C:R, D:R */

	_wait_async_thread(AS_GETDATA);  /* another get data attempt, and it will return not ready at this point */
	_unblock_async_thread(0);
	_wait_async_thread(AS_WRITE);
	_unblock_async_thread(0);
	_wait_async_thread(AS_WR_FIN);   /* it should do the actual write operation */
	ASSERT(_as_u16(&data, 1) == _as_u16(conn[0].buf, 0), CLEANUP_NOP);
	_unblock_async_thread(0);
	/* C:R, D:R */

	_wait_async_thread(AS_GETDATA);  /* Another get data attempt, because the connection is still in reday state */
	_unblock_async_thread(0);
	/* at this point buffer is empty and data source not ready, wait for data, no actual write at this point */
	/* C:R, D:W */
	/* nothing could happen at this point */
	usleep(1000);

	ASSERT_OK(module_tcp_async_write_data_ready(loop, 0), CLEANUP_NOP);
	_wait_async_thread(AS_GETDATA);   /* the data ready call will make the asnyc loop weakup */
	dh[0].stage = 2;
	_unblock_async_thread(0);         /* now it should have data */
	/* C:R D:R */
	_wait_async_thread(AS_WRITE);
	conn[conn_id].bytes_to_accept = 4;
	_unblock_async_thread(0);
	_wait_async_thread(AS_WR_FIN);
	ASSERT(2 == _as_u32(conn[0].buf, 0), CLEANUP_NOP);
	_unblock_async_thread(0);

	/* C:R D:R */
	ASSERT_OK(module_tcp_async_write_data_ends(loop, 0), CLEANUP_NOP);  /* make the connection in the end state */
	_wait_async_thread(AS_GETDATA);   /* another get data attempt */
	dh[0].stage = 4;
	_unblock_async_thread(0);         /* return another 4 bytes */
	_wait_async_thread(AS_WRITE);     /* another write attempt */
	_unblock_async_thread(0);
	_wait_async_thread(AS_WR_FIN); /* the actual write should perfomed */
	ASSERT(4 == _as_u32(conn[0].buf, 0), CLEANUP_NOP);
	_unblock_async_thread(0);

	/* C:R, D:R */
	_wait_async_thread(AS_GETDATA); /* another get data attempt */
	dh[0].stage = 1;                /* end of data source */
	_unblock_async_thread(0);       /* no data at all */

	_wait_async_thread(AS_DISPOSE);
	_unblock_async_thread(0);

	ASSERT(1 == dh[0].disposed, CLEANUP_NOP);
	ASSERT(0 == dh[0].error, CLEANUP_NOP);

	return 0;
}

static inline int _parallel_write(uint32_t n)
{
	uint32_t i;
	for(i = 0; i < n; i ++)
	{
		_set_block_bits(i, 0xffffffff);
		ASSERT_OK(module_tcp_async_write_register(loop, i, conn[i].efd, 16, _get_data_1, _dispose_handler_1, _error_handler_1, dh + i), CLEANUP_NOP);
	}

	usleep(1000); /* make sure we do not have no operations, if this is not true, async thread will blocked */

	/* set data for [n/2, n) to connection ready */
	for(i = n / 2; i < n; i ++)
	{
		_set_connction_busy(i, 0);
		conn[i].bytes_to_accept = 1;
	}

	/* no data source is ready, so shouldn't have any ops here */
	usleep(1000); /* make sure we do not have no operations*/

	/* connection [0, n/2) has data ready, and makes connection 0, 2, 4, 8, ... are ready */
	for(i = 0; i < n / 2; i ++)
	{
		dh[i].stage = 1234 * i;
		ASSERT_OK(module_tcp_async_write_data_ready(loop, i), CLEANUP_NOP);
		if(i%2 == 0) _set_connction_busy(i, 0);
		conn[i].bytes_to_accept = 4;
	}


	uint32_t visit[n];
	memset(visit, 0, sizeof(visit));
	/* after that we should have GETDATA attempts on [0, n/2)*/
	for(i = 0; i < n / 2; i ++)
	{
		_wait_async_thread(AS_GETDATA);
		uint32_t this = conn_id;
		ASSERT(this < n / 2, CLEANUP_NOP);
		ASSERT(visit[this] == 0, CLEANUP_NOP);
		visit[this] = 1;
		_unblock_async_thread(conn_id);

		_wait_async_thread(AS_WRITE);
		ASSERT(conn_id == this, CLEANUP_NOP);
		if(conn_id % 2 == 0)
		{
			_unblock_async_thread(conn_id);
			_wait_async_thread(AS_WR_FIN);
			ASSERT(_as_u32(conn[conn_id].buf, 0) == conn_id * 1234, CLEANUP_NOP);
			_unblock_async_thread(conn_id);
		}
		else _unblock_async_thread(conn_id);
	}

	/* then, we have connection 1, 3, 5,.... has a waiting for connection state */

	/* makes all connections has data */
	for(i = 0; i < n; i ++)
	{
		if(i < n / 2 && i % 2 == 1) continue;
		dh[i].stage = 1111 * i;
		ASSERT_OK(module_tcp_async_write_data_ready(loop, i), CLEANUP_NOP);
	}

	/* now we have 0,2,4,6,8,...., n/2, n/2 + 1, ...., n - 1 in ready state */

	uint32_t count[n];
	memset(count, 0, sizeof(count));
	memset(visit, 0, sizeof(visit));
	for(i = 0; i < n - n / 4; i ++)
	{
		_wait_async_thread(AS_GETDATA);
		LOG_DEBUG("waiting for thread %d", conn_id);
		uint32_t this = conn_id;
		ASSERT(this % 2 == 0 || this >= n / 2, CLEANUP_NOP);
		ASSERT(visit[this] == 0, CLEANUP_NOP);
		visit[this] = 1;

		if(conn_id % 2 == 0)
		{
			_unblock_async_thread(conn_id);
			_wait_async_thread(AS_WRITE);
			ASSERT(conn_id == this, CLEANUP_NOP);
			_unblock_async_thread(conn_id);
			_wait_async_thread(AS_WR_FIN);
			if(conn_id >= n / 2)
			{
				uint32_t expected = conn_id * 1111;
				uint8_t* arr = (uint8_t*)&expected;
				ASSERT(*(uint8_t*)conn[conn_id].buf == arr[count[conn_id]], CLEANUP_NOP);
			}
			else
			{
				ASSERT(_as_u32(conn[conn_id].buf, 0) == conn_id * 1111, CLEANUP_NOP);
			}
			count[conn_id] = (count[conn_id] + 1) % sizeof(int);
			_unblock_async_thread(conn_id);
		}
		else _unblock_async_thread(conn_id);
	}

	/* now we have all the even numbered connection in ready state,
	 * odd connection smaller than n/2 in waiting for connection state
	 * odd connection larger/equal than n/2 in waiting for data state */
	/* now let the all the even numbered connection write 10 times */
	for(i = 0; i < n; i ++)
	    _set_block_bits(i, 0xffffffff ^ AS_GETDATA);  /* unblock the get data call */

	for(i = 0; i < 10; i ++)
	{
		uint32_t j;
		for(j = 0; j < n / 2; j ++)
		{
			_wait_async_thread(AS_WRITE);
			_unblock_async_thread(conn_id);
			_wait_async_thread(AS_WR_FIN);
			if(conn_id >= n / 2)
			{
				uint32_t expected = conn_id * 1111;
				uint8_t* arr = (uint8_t*)&expected;
				ASSERT(*(uint8_t*)conn[conn_id].buf == arr[count[conn_id]], CLEANUP_NOP);
			}
			else
			{
				ASSERT(_as_u32(conn[conn_id].buf, 0) == conn_id * 1111, CLEANUP_NOP);
			}
			count[conn_id] = (count[conn_id] + 1) % sizeof(int);
			_unblock_async_thread(conn_id);
		}
	}

	/* then do following things:
	 * 1. make all the even data source not ready
	 * 2. make all the odd connection samller than n/2 in ready state */

	for(i = 0; i < n / 2; i ++)
	{
		dh[i * 2].stage = 1;
		if(i % 2 == 1)
		    _set_connction_busy(i, 0);
	}


	/* at this place, we have even connection smaller than n/2 has the state of waiting for data and
	 * even connection larger/equal than n/2 still has data to consume [11,16). So repeat 6 more times */
	for(i = 0; i < 4; i ++)
	{
		LOG_DEBUG("=================================================================");
		uint32_t j;
		for(j = 0; j < n / 2; j ++)
		{
			_wait_async_thread(AS_WRITE);
			_unblock_async_thread(conn_id);
			_wait_async_thread(AS_WR_FIN);
			if(conn_id < n / 2 && conn_id % 2 == 1)
			{
				ASSERT(_as_u32(conn[conn_id].buf, 0) == 1234u * conn_id, CLEANUP_NOP);
			}
			else if(conn_id >= n / 2)
			{
				uint32_t expected = conn_id * 1111;
				uint8_t* arr = (uint8_t*)&expected;
				ASSERT(*(uint8_t*)conn[conn_id].buf == arr[count[conn_id]], CLEANUP_NOP);
			}
			count[conn_id] = (count[conn_id] + 1) % sizeof(int);
			_unblock_async_thread(conn_id);
		}
	}

	/* then make the connection 0 returns an error */
	while(1)
	{
		_wait_async_thread(AS_WRITE);   /* next time it must be a write */
		if(conn_id != 1)
		{
			conn[1].mocked_err = EINVAL;
			for(i = 0; i < n; i ++)
			    _set_block_bits(i, AS_DISPOSE);
			_set_block_bits(1, AS_WRITE | AS_ERROR | AS_DISPOSE);
			_unblock_async_thread(conn_id);
			break;
		}
		_unblock_async_thread(conn_id);
		_wait_async_thread(AS_WR_FIN);
		_unblock_async_thread(conn_id);
	}

	while(1)
	{
		_wait_async_thread(AS_WRITE);
		uint32_t this = conn_id;
		if(this == 1) break;
	}

	_unblock_async_thread(1);

	_wait_async_thread(AS_ERROR);
	ASSERT(conn_id == 1, CLEANUP_NOP);
	_unblock_async_thread(1);

	for(i = 0; i < n; i ++)
	{
		dh[i].stage = 1;
		ASSERT_OK(module_tcp_async_write_data_ends(loop, i), CLEANUP_NOP);
	}

	for(i = 0; i < n; i ++)
	{
		_wait_async_thread(AS_DISPOSE);
		_unblock_async_thread(conn_id);
	}

	return 0;
}

int parallel_write()
{
	ASSERT_OK(_parallel_write(100), CLEANUP_NOP);
	return 0;
}

int setup()
{
	expected_memory_leakage();

	unsigned i;
	for(i = 0; i < sizeof(conn) / sizeof(*conn); i ++)
	{
#ifdef __LINUX__
		ASSERT((conn[i].efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) >= 0, CLEANUP_NOP);
#else
		ASSERT(pipe(conn[i].pipe) >= 0, CLEANUP_NOP);
		conn[i].efd = conn[i].pipe[0];
#endif
		conn[i].busy = 1;
		conn[i].block = 1;
		ASSERT_OK(pthread_mutex_init(&conn[i].mutex, NULL), CLEANUP_NOP);
		ASSERT_OK(pthread_cond_init(&conn[i].cond, NULL), CLEANUP_NOP);
	}
	ASSERT_OK(pthread_mutex_init(&sync_mutex, NULL), CLEANUP_NOP);
	ASSERT_OK(pthread_cond_init(&sync_cond, NULL), CLEANUP_NOP);

	return 0;
}

int teardown()
{
	unsigned i;
	for(i = 0; i < sizeof(conn) / sizeof(*conn); i ++)
	{
#ifdef __LINUX__
		ASSERT_OK(close(conn[i].efd), CLEANUP_NOP);
#else
		ASSERT_OK(close(conn[i].pipe[0], CLEANUP_NOP));
		ASSERT_OK(close(conn[i].pipe[1], CLEANUP_NOP));
#endif
		ASSERT_OK(pthread_mutex_destroy(&conn[i].mutex), CLEANUP_NOP);
		ASSERT_OK(pthread_cond_destroy(&conn[i].cond), CLEANUP_NOP);
	}
	ASSERT_OK(pthread_mutex_destroy(&sync_mutex), CLEANUP_NOP);
	ASSERT_OK(pthread_cond_destroy(&sync_cond), CLEANUP_NOP);
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(create_loop),
    TEST_CASE(single_async_write),
    TEST_CASE(parallel_write),
    TEST_CASE(cleanup_loop)
TEST_LIST_END;
