/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <inttypes.h>

#include <barrier.h>
#include <error.h>
#include <utils/log.h>
#include <utils/thread.h>
#include <utils/static_assertion.h>
#include <module/tcp/async.h>
#include <utils/mempool/page.h>
#include <os/os.h>

/**
 * @brief the state of an async object
 * @note there are 3 possible states. When connection is not ready, it means we
 *       have a failed write, which implies that we have data that is not yet written
 *       to the connection. In this case the data is definitely ready. <br/>
 *       On the other hand, if data is ready, the connection status may be either ready
 *       or wait. But this is not important, because even if the connection is not ready
 *       we are able to mark it to ready whenever the data is ready, and have one more
 *       write failure. And then the async object will be set to _ST_WAIT_CONN. <br/>
 *       Possible state transition: <br/>
 *
 *       (async object creation) =&gt; _ST_WAIT_DATA  //when async object is created <br/>
 *       _ST_WAIT_DATA =&gt; _ST_READY                //when data is ready <br/>
 *       _ST_READY =&gt; _ST_WAIT_CONN                //when socket returns EAGAIN or EWOULDBLOCK<br/>
 *       _ST_READY =&gt; _ST_WAIT_DATA                //when there's no data to write and the data source is still active <br/>
 *       _ST_READY =&gt; _ST_FINISHED                 //when there's no data and data end message have seen <br/>
 *       _ST_READY =&gt; _ST_RAISING                  //when an write error hanppens <br/>
 *       _ST_RAISING =&gt; _ST_ERROR                  //when the raised error has been handled <br/>
 *       _ST_WAIT_CONN =&gt; _ST_READY                //when connection gets ready at this moment <br/>
 **/
typedef enum {
	_ST_WAIT_CONN, /*!< the async object is waiting for connection, but data is ready */
	_ST_WAIT_DATA, /*!< the async object is waiting for data, and FD status is unknown */
	_ST_READY,     /*!< the async object is ready to perform IO operation */
	_ST_RAISING,   /*!< the async object which has an unhandled error with it */
	_ST_ERROR,     /*!< error state */
	_ST_FINISHED,  /*!< the async object is finished */
	_NUM_OF_STATES /*!< the number of states */
} _async_obj_state_t;
/* We assume that _ST_WAIT_CONN is the first state in the st_list, because it should maintain a
 * binary heap on that, and we basically want to make sure all the data moves into this section
 * is the final destination. */
STATIC_ASSERTION_EQ(_ST_WAIT_CONN, 0);

/**
 * @brief the human readable state string
 **/
const char* _async_obj_state_str[] = {
	[_ST_WAIT_DATA] = "WAIT_DATA",
	[_ST_WAIT_CONN] = "WAIT_CONN",
	[_ST_READY]     = "READY  ",
	[_ST_ERROR]     = "ERROR  ",
	[_ST_FINISHED]  = "FINISHED",
	[_ST_RAISING]   = "RAISING_ERR"
};

/**
 * @brief an async object
 **/
typedef struct {
	/** @note This flag bit allows us to limit the size of the message queue to _NUM_CONN_MSG_TYPS * pool_size + 1,
	 *        if we do not duplicate the _MT_READY message if there's another on in the queue. <br/>
	 *        The reason why we do not have such bit on other queue messages is following <br/>
	 *        The _MT_CREATE message only can be posted when the async operation begins, this means
	 *        until the connection object has been released (the _MT_END message has been processed)
	 *        there shouldn't be any other _MT_CREATE on the same connection object. <br/>
	 *        For the same reason, unless the previous _MT_END message is processed, there shouldn't
	 *        be any other _MT_END on the same connection object. <br/>
	 *        For _MT_KILL, it doesn't matter anyway, since it's the last message this loop processes.
	 */
	uint32_t                                   rdy_posted:1; /*!< if the queue ready message is posted and in pending state */
	uint32_t                                   index;        /*!< the index in the async state list */
	time_t                                     ts;           /*!< the timestamp when it entering _ST_WAIT_CONN state (only valid for _ST_WAIT_CONN state) */
	int                                        fd;           /*!< the coresponding fd */
	int                                        data_end;     /*!< indicates if there's no more data ready events */
	module_tcp_async_write_data_func_t         get_data;     /*!< the data source callback */
	module_tcp_async_write_cleanup_func_t      cleanup;      /*!< the cleanup callback */
	module_tcp_async_write_error_func_t        onerror;      /*!< the error handler */
	void*                                      handle;       /*!< the data handle */
	size_t                                     b_size;       /*!< the buffer size */
	size_t                                     b_begin;      /*!< the io buffer begin */
	size_t                                     b_end;        /*!< the io buffer end */
	char*                                      io_buffer;    /*!< the io buffer */
} _async_obj_t;

/**
 * @brief the message type
 **/
typedef enum {
	_MT_CREATE,  /*!< new async object has been created */
	_MT_READY,   /*!< the message for data ready notification */
	_MT_END,     /*!< the message for data end notification */
	_NUM_CONN_MSG_TYPS, /*!< the number of message types that is actually related to the connection object */
	_MT_KILL     /*!< indicates we want to kill the loop */
} _message_type_t;
STATIC_ASSERTION_EQ(_MT_KILL, _NUM_CONN_MSG_TYPS + 1);

/**
 * @brief the human readable  string for a message type
 **/
const char* _message_str[] = {
	[_MT_CREATE]   = "CREATE_ASYNC_OBJ",
	[_MT_READY]    = "DATA_REDAY",
	[_MT_END]      = "DATA_END",
	[_MT_KILL]     = "LOOP_KILLED"
};

/**
 * @brief a message in the queue
 **/
typedef struct _message_t {
	_message_type_t    type;    /*!< the message type */
	uint32_t           conn_id; /*!< the target connection id */
} _message_t;

/**
 * @brief the actual data structure for an async loop
 **/
struct _module_tcp_async_loop_t {
	/* State bits */
	uint32_t     killed:1;     /*!< if this loop gets killed */
	uint32_t     started:1;    /*!< if the loop is already started */
	uint32_t     i_q_mutex:1;  /*!< if the q_mutex has been  initialized */
	uint32_t     i_s_mutex:1;  /*!< if the s_mutex has been initialized */
	uint32_t     i_s_cond:1;   /*!< if the s_cond has been initialized */

	/* Data related fields */
	uint32_t     capacity;     /*!< the max size of this async loop */
	_async_obj_t* objects;     /*!< the object list, which is addressed by the conn_id */
	uint32_t*    st_list;      /*!< the state list, which organize the conn_id in to group in which async object has same state */
	uint32_t     limits[_NUM_OF_STATES]; /*!< the array manipuates the end indices of each state */

	/* File descriptors */
	os_event_poll_t* poll;      /*!< The poll object */
	int          event_fd;     /*!< the event fd used to notify events */
	uint32_t     max_events;   /*!< the size of event buffer */

	/* Thread */
	thread_t*    loop;         /*!< the thread object for this loop */

	/* The notification queue */
	_message_t*     queue;     /*!< the message queue, as we can see, the limit of the queue size is _NUM_CONN_MSG_TYPS * pool_size + 1 */
	uint32_t        q_mask;    /*!< the mask used to get the queue offset address */
	uint32_t        q_front;   /*!< the front serial number for this queue */
	uint32_t        q_rear;    /*!< the rear serial number for this queue */

	pthread_mutex_t q_mutex;   /*!< the mutex used when modifying the queue */

	/* thread syhchronization */
	pthread_mutex_t s_mutex;   /*!< the startup mutex */
	pthread_cond_t  s_cond;    /*!< the startup condition variable */

	/* connection options */
	time_t          ttl;       /*!< the maximum time for a connection be busy state */
	/* mocked system calls */
	ssize_t (*write)(int fd, const void* ptr, size_t sz);  /*!< the mocked write system call, only used for testing purpose */
};

/**
 * @brief get how many async object is currently in the given state
 * @param loop the async loop
 * @param state the target state
 * @return the number of objects
 **/
static inline uint32_t _get_num_async_in_state(const module_tcp_async_loop_t* loop, _async_obj_state_t state)
{
	uint32_t begin = 0;
	if(state > 0) begin = loop->limits[state - 1];
	uint32_t end = loop->limits[state];
	return end - begin;
}
/**
 * @brief get the connection object id from the async object
 * @param loop the async loop
 * @param async the async object
 * @return the result connection object id
 **/
static inline uint32_t _async_obj_conn_id(const module_tcp_async_loop_t* loop, const _async_obj_t* async)
{
	return (uint32_t)(async - loop->objects);
}
/**
 * @brief get the current state of the connection
 * @param loop the async loop
 * @param async the async object
 * @return the state of this async object
 **/
static inline _async_obj_state_t _async_obj_get_state(const module_tcp_async_loop_t* loop, const _async_obj_t* async)
{
	uint32_t i = async->index;
	_async_obj_state_t ret;
	for(ret = 0; ret < _NUM_OF_STATES && loop->limits[ret] <= i; ret ++);

	if(ret == _NUM_OF_STATES)
	    LOG_ERROR("unexpected type of async object");

	return ret;
}

/**
 * @brief swap two items in the state list
 * @param loop the async loop
 * @param a the index of first item in state list
 * @param b the index of second item in state list
 * @return nothing
 **/
static inline void _swap(module_tcp_async_loop_t* loop, uint32_t a, uint32_t b)
{
	/* swap the state list */
	uint32_t tmp = loop->st_list[a];
	loop->st_list[a] = loop->st_list[b];
	loop->st_list[b] = tmp;

	/* maintain the index field of each async object */
	loop->objects[loop->st_list[a]].index = a;
	loop->objects[loop->st_list[b]].index = b;
}
/**
 * @brief get the begin index of the given state (include the first element)
 * @param loop the async loop
 * @param st the target state
 * @return the result index
 **/
static inline uint32_t _async_obj_state_begin(const module_tcp_async_loop_t* loop, _async_obj_state_t st)
{
	if(st == 0) return 0;
	return loop->limits[st - 1];
}
/**
 * @brief get the end index of the give state (exclude the last element)
 * @param loop the async loop
 * @param st the target state
 * @return the result index
 **/
static inline uint32_t _async_obj_state_end(const module_tcp_async_loop_t* loop, _async_obj_state_t st)
{
	return loop->limits[st];
}
/**
 * @brief heapify: downward adjustment on a wait connection list
 * @param loop the async loop
 * @param idx the index of the target node
 * @return nothing
 **/
static inline void _async_wait_conn_heapify(module_tcp_async_loop_t* loop, uint32_t idx)
{
	for(;idx < loop->limits[_ST_WAIT_CONN];)
	{
		uint32_t m_idx = idx;
		if(idx * 2 + 1 < loop->limits[_ST_WAIT_CONN] &&
		   loop->objects[loop->st_list[idx * 2 + 1]].ts < loop->objects[loop->st_list[m_idx]].ts)
		    m_idx = idx * 2 + 1;
		if(idx * 2 + 2 < loop->limits[_ST_WAIT_CONN] &&
		   loop->objects[loop->st_list[idx * 2 + 2]].ts < loop->objects[loop->st_list[m_idx]].ts)
		    m_idx = idx * 2 + 2;
		if(m_idx == idx) return;
		_swap(loop, m_idx, idx);
		idx = m_idx;
	}
}
/**
 * @brief decrease: upward adjustment on a wait connection list
 * @param loop the async loop
 * @param idx the index of the target node
 * @return nothing
 **/
static inline void _async_wait_conn_decrease(module_tcp_async_loop_t* loop, uint32_t idx)
{
	for(;idx > 0 && loop->objects[loop->st_list[(idx - 1)/2]].ts > loop->objects[loop->st_list[idx]].ts; idx = (idx - 1) / 2)
	    _swap(loop, idx, (idx - 1) / 2);
}

/**
 * @brief set the state of the async object
 * @param loop the async loop
 * @param async the async object
 * @param state the new state
 * @return status code
 **/
static inline int  _async_obj_set_state(module_tcp_async_loop_t* loop, _async_obj_t* async, _async_obj_state_t state)
{
	if(state >= _NUM_OF_STATES)
	    ERROR_RETURN_LOG(int, "invalid state");

	_async_obj_state_t cur_st = _async_obj_get_state(loop, async);

	if(cur_st >= _NUM_OF_STATES)
	    ERROR_RETURN_LOG(int, "cannot get current state for connection %"PRIu32, _async_obj_conn_id(loop, async));

	if(cur_st == state) return 0;

	LOG_DEBUG("setting the state of connection object %"PRIu32" from %s to %s",
	          _async_obj_conn_id(loop, async), _async_obj_state_str[cur_st],
	          _async_obj_state_str[state]);

	if(state < cur_st)
	{
		/* move forward */
		for(;state < cur_st; cur_st --)
		{
			uint32_t begin = 0;
			_async_obj_state_t prev_st = cur_st - 1;
			/* GCC is too stupid to realize cur_st's range is [1, _NUM_OF_STATES) */
			if(prev_st >= _NUM_OF_STATES) __builtin_unreachable();

			/* Because the state must be different from the cur_st, so it should always larger than 1 */
			begin = loop->limits[prev_st];
			_swap(loop, async->index, begin);
			loop->limits[prev_st] ++;
		}

		if(state == _ST_WAIT_CONN)
		{
			/* if this connection is being adding to the wait connection state, maintain the heap property */
			async->ts = time(NULL);
			_async_wait_conn_decrease(loop, async->index);
		}
	}
	else
	{
		if(cur_st == _ST_WAIT_CONN)
		{
			/* if this connection is in wait connection state, remove it from the heap */
			uint32_t cur_idx = async->index;
			_swap(loop, cur_idx, loop->limits[_ST_WAIT_CONN] - 1);
			loop->limits[_ST_WAIT_CONN] --;
			_async_wait_conn_heapify(loop, cur_idx);
			cur_st ++;
		}
		/* move backward */
		for(; cur_st < state; cur_st ++)
		{
			/* Because current state has at least one elelement, which is the async object itself
			 * So there must be a last async object in this state. */
			uint32_t end = loop->limits[cur_st] - 1;
			_swap(loop, async->index, end);
			/* because state is different than cur_st, and it's less than cur_state
			 * so it can not be the last state. So we can do this very safe */
			loop->limits[cur_st] --;
		}
	}

	return 0;
}
/**
 * @brief get the async object from the index in state array
 * @param loop the async loop
 * @param idx the index in the async object loop
 * @return the target async object, NULL on error
 **/
static inline _async_obj_t* _async_obj_get_from_index(module_tcp_async_loop_t* loop, uint32_t idx)
{

	if(idx >= loop->limits[_NUM_OF_STATES - 1])
	    ERROR_PTR_RETURN_LOG("invalid items in the async object state array");

	_async_obj_t* ret = loop->objects + loop->st_list[idx];

	if(ret->index == ERROR_CODE(uint32_t))
	    ERROR_PTR_RETURN_LOG("connection object %"PRIu32" have no async object attached", loop->st_list[idx]);

	return ret;
}
/**
 * @brief performe the IO operations
 * @param loop the async loop
 * @param obj the async object
 * @return the new state for this object, or error code
 **/
static inline _async_obj_state_t _io_ops(module_tcp_async_loop_t* loop, _async_obj_t* obj)
{
	if(obj->b_end == obj->b_begin) obj->b_begin = obj->b_end = 0;

	/* before we perform the actual data operation, we want to maximize the number of bytes passed to the system call */
	if(obj->b_end < obj->b_size)
	{
		size_t rdsz = obj->get_data(_async_obj_conn_id(loop, obj), obj->io_buffer + obj->b_end, obj->b_size - obj->b_end, loop);
		if(ERROR_CODE(uint32_t) == rdsz)
		{
			LOG_ERROR("the data function returns an error code, "
			          "set the async object %"PRIu32" state to ERROR",
			          _async_obj_conn_id(loop, obj));
			return _ST_RAISING;
		}
		LOG_DEBUG("Read %zu bytes from the data source callback", rdsz);
		obj->b_end += rdsz;
	}
	else
	    LOG_DEBUG("Connection object %"PRIu32": there's no space for the new data in the buffer, "
	              "consuming the existing data first", _async_obj_conn_id(loop,obj));

	LOG_DEBUG("Connection object %"PRIu32": buffer range [%zu, %zu)",
	          _async_obj_conn_id(loop, obj), obj->b_begin, obj->b_end);

	/* Still nothing to write, means we are either finish writing or we should wait for data gets ready */
	if(obj->b_end - obj->b_begin == 0)
	{
		if(!obj->data_end)
		{
			LOG_DEBUG("data is not available for connection object %"PRIu32", "
			          "updating the state of async object to WAIT_FOR_DATA",
			          _async_obj_conn_id(loop, obj));
			return _ST_WAIT_DATA;
		}
		else
		{
			LOG_DEBUG("connection object %"PRIu32" has been released by the module "
			          "and data buffer exhausted, updating the state to FINISHED",
			          _async_obj_conn_id(loop, obj));
			return _ST_FINISHED;
		}
	}

	/* call the system call */
	ssize_t rc = loop->write == NULL ?
	             write(obj->fd, obj->io_buffer + obj->b_begin, obj->b_end - obj->b_begin):
	             loop->write(obj->fd, obj->io_buffer + obj->b_begin, obj->b_end - obj->b_begin);

	if(-1 == rc || rc == 0)
	{
		if(errno == EWOULDBLOCK || errno == EAGAIN)
		{
			LOG_DEBUG("connection object %"PRIu32" is busy, "
			          "update the state to WAIT_FOR_CONNECTION",
			          _async_obj_conn_id(loop, obj));
			return _ST_WAIT_CONN;
		}
		else
		{
			LOG_ERROR_ERRNO("connection object %"PRIu32" has a write failure, "
			                "update the state to ERROR",
			                _async_obj_conn_id(loop, obj));
			return _ST_RAISING;
		}
	}
	else
	{
		LOG_DEBUG("%zd bytes has been written to the connection object %"PRIu32, rc, _async_obj_conn_id(loop, obj));
		obj->b_begin += (uint32_t)rc;
	}

	return _ST_READY;
}
/**
 * @brief add the async object to the poll object's wait list
 * @param loop the async loop
 * @param async the async object to add
 * @return status code
 **/
static inline int _async_obj_add_poll(module_tcp_async_loop_t* loop, _async_obj_t* async)
{
	os_event_desc_t event = {
		.type = OS_EVENT_TYPE_KERNEL,
		.kernel = {
			.event = loop->write == NULL ? OS_EVENT_KERNEL_EVENT_OUT :
			                               OS_EVENT_KERNEL_EVENT_IN,
			.fd    = async->fd,
			.data  = async
		}
	};
	if(ERROR_CODE(int) == os_event_poll_add(loop->poll, &event))
	    ERROR_RETURN_LOG(int, "Cannot add the async object to the poll wait list");

	LOG_DEBUG("Connection object %"PRIu32" has been added to the poll wait list", _async_obj_conn_id(loop, async));

	return 0;
}

/**
 * @brief remove the async object from the poll's wait list
 * @param loop the async loop
 * @param async the async object
 * @return status code
 **/
static inline int _async_obj_del_poll(module_tcp_async_loop_t* loop, _async_obj_t* async)
{
	if(ERROR_CODE(int) == os_event_poll_del(loop->poll, async->fd, loop->write == NULL ? 0 : 1))
	    ERROR_RETURN_LOG(int, "Cannot delete the async object from the poll wait list");
	return 0;
}

static inline void _print_async_obj_layout(module_tcp_async_loop_t* loop)
{
	(void) loop;
	LOG_DEBUG("Async IO Objects Layout:");
	_async_obj_state_t i;
	for(i = 0; i < _NUM_OF_STATES; i ++)
	{
		LOG_DEBUG("\t%s:\t[%"PRIu32", %"PRIu32")",
		          _async_obj_state_str[i],
		          _async_obj_state_begin(loop, i),
		          _async_obj_state_end(loop, i));
	}
}

/**
 * @brief for each write iteration, this function will gets called once and
 *        it will scan the async object list, and performe operations based
 *        on the current state
 * @param loop the async loop
 * @return status code
 **/
static inline int _process_async_objs(module_tcp_async_loop_t* loop)
{
	/* write each ready connection */
	uint32_t i;
	for(i = _async_obj_state_begin(loop, _ST_READY);
	    i < _async_obj_state_end(loop, _ST_READY);
	    i ++)
	{
		_async_obj_t* this = _async_obj_get_from_index(loop, i);
		if(NULL == this)
		{
			LOG_WARNING("cannot get the async object at index %"PRIu32, i);
			continue;
		}

		_async_obj_state_t next_st;
		if((next_st = _io_ops(loop, this)) == ERROR_CODE(_async_obj_state_t))
		{
			LOG_WARNING("Cannot finish the async IO operation for connection object %"PRIu32, _async_obj_conn_id(loop, this));
			continue;
		}

		LOG_DEBUG("IO iteration finished for connection object %"PRIu32" next state: %s",
		          _async_obj_conn_id(loop, this), _async_obj_state_str[next_st]);

		/* Move the async object according to the new state, so if a async object is not ready anymore, it won't get chance to write next time */
		if(_async_obj_set_state(loop, this, next_st) == ERROR_CODE(int))
		{
			LOG_WARNING("cannot set the state of the async object for connection %"PRIu32, _async_obj_conn_id(loop, this));
			continue;
		}


		/* If the state has been moved backward, the current cell will have a new item
		 * It's ok for the last item, because although we did so, but in the next iteration,
		 * The index will be out of the ready state. After this, the loop will terminate.
		 * For the moving forward case, it doesn't matter anyway, because we scan it from
		 * the begining to the end. */
		if(next_st > _ST_READY) i --;

		/* If the new state incidates the connection is not ready, add it to the wait list */
		if(next_st == _ST_WAIT_CONN && _async_obj_add_poll(loop, this) == ERROR_CODE(int))
		    LOG_WARNING("cannot add the async object to the waiting list");
	}


	/* call the error handler for the error raising async calls */
	for(i = _async_obj_state_begin(loop, _ST_RAISING);
	    i < _async_obj_state_end(loop, _ST_RAISING);
	    i ++)
	{
		_async_obj_t* this = _async_obj_get_from_index(loop, i);
		if(NULL == this)
		{
			LOG_WARNING("cannot get the async object from at index %"PRIu32, i);
			continue;
		}

		uint32_t conn_id = _async_obj_conn_id(loop, this);

		LOG_DEBUG("calling error handler of connection object %"PRIu32, conn_id);

		if(this->onerror(conn_id, loop) == ERROR_CODE(int))
		    LOG_WARNING("error while executing the error handler for connection object %"PRIu32, conn_id);

		/* If the data end message has sent previously, that means we should change the state to finished now.
		 * Otherwise, the data end message is expected, so that we can update its state to finished at the time
		 * the connection gets the message */
		if(this->data_end)
		{
			LOG_DEBUG("the conneciton object %"PRIu32" encounter a write error, "
			          "and the data_end message is already processed, setting the"
			          "connection state to %s", conn_id, _async_obj_state_str[_ST_FINISHED]);
			if(_async_obj_set_state(loop, this, _ST_FINISHED) == ERROR_CODE(int))
			{
				LOG_WARNING("Cannot set the connection object %"PRIu32, conn_id);
				continue;
			}
			if(_ST_FINISHED > _ST_RAISING) i --;
		}
		else
		{
			LOG_DEBUG("the connection object %"PRIu32" encounter a write error, "
			          "and the data_end message is not processed yet, set the connection"
			          "state to %s and wait for data_end message", conn_id, _async_obj_state_str[_ST_ERROR]);
			if(_async_obj_set_state(loop, this, _ST_ERROR) == ERROR_CODE(int))
			{
				LOG_WARNING("Cannot set the connection object %"PRIu32, conn_id);
				continue;
			}
			if(_ST_ERROR > _ST_RAISING) i --;
		}

		LOG_TRACE("Async IO operation for connection object %"PRIu32" is entering an error state", conn_id);
	}

	/* finialize all fnished async objects */
	for(i = _async_obj_state_begin(loop, _ST_FINISHED);
	    i < _async_obj_state_end(loop, _ST_FINISHED);
	    i ++)
	{
		_async_obj_t* this = _async_obj_get_from_index(loop, i);
		if(NULL == this)
		{
			LOG_WARNING("cannot get the async object from at index %"PRIu32, i);
			continue;
		}

		uint32_t conn_id = _async_obj_conn_id(loop, this);

		LOG_DEBUG("handling the async object in finished state for connection object %"PRIu32, conn_id);

		//free(this->io_buffer);
		if(ERROR_CODE(int) == mempool_page_dealloc(this->io_buffer))
		    LOG_ERROR("Cannot deallocte the io buffer page");

		this->index = ERROR_CODE(uint32_t);

		if(!this->data_end) LOG_ERROR("a finished async object without data_end flag, code bug!");
		BARRIER();
		/* Make sure cleanup call back will be called in the last. Because cleanup callback will release
		 * the connection object, which means it's possible to re-register the callback function back once
		 * we called this.
		 * And it so, we have a race condition, because we are still working on this connection object releated
		 * async object, but the new async object request is coming.
		 * So it's dengours, because the index array is not yet set to ERROR_CODE(uint32_t), but the new reigsteration
		 * comes in, and it wil fails */
		if(this->cleanup(conn_id, loop) == ERROR_CODE(int))
		    LOG_WARNING("error while executing the cleanup callback for connection object %"PRIu32, conn_id);

		LOG_TRACE("Async IO operation for connection object %"PRIu32" finished", conn_id);
	}

	/* finally, remove all the finished connections */
	_async_obj_state_t st;
	uint32_t fin_size = _get_num_async_in_state(loop, _ST_FINISHED);
	/* If the finished state is not the last state, move the remaning data */
	if(_ST_FINISHED + 1 < _NUM_OF_STATES)
	{
		memmove(loop->st_list + _async_obj_state_begin(loop, _ST_FINISHED),
		        loop->st_list + _async_obj_state_end(loop, _ST_FINISHED),
		        sizeof(uint32_t) * (loop->limits[_NUM_OF_STATES - 1] - loop->limits[_ST_FINISHED]));
	}
	for(st = _ST_FINISHED; st < _NUM_OF_STATES; st ++)
	    loop->limits[st] -= fin_size;

	LOG_DEBUG("Async IO iteration finished");
	_print_async_obj_layout(loop);

	return 0;
}
static inline int _process_queue_message(module_tcp_async_loop_t* loop)
{
	if(ERROR_CODE(int) == os_event_user_event_consume(loop->poll, loop->event_fd))
	    ERROR_RETURN_LOG(int, "Cannot consume user event");

	LOG_DEBUG("New incoming queue message");
	if(pthread_mutex_lock(&loop->q_mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "cannot acquire the global queue mutex");

	for(;loop->q_front != loop->q_rear; loop->q_front ++)
	{
		const _message_t* current = loop->queue + (loop->q_front & loop->q_mask);

		LOG_DEBUG("Executing QM @ %"PRIu32": <type=%s, conn=%"PRIu32">", loop->q_front, _message_str[current->type], current->conn_id);

		_async_obj_t* async = loop->objects + current->conn_id;
		if(current->type != _MT_KILL &&
		   current->type != _MT_CREATE &&
		   async->index == ERROR_CODE(uint32_t))
		{
			LOG_ERROR("connction object %"PRIu32" has no undergoing async ops, code bug!", current->conn_id);
			continue;
		}
		if(current->type == _MT_READY)
		{
			LOG_DEBUG("QM: data ready notification on connection object %"PRIu32, current->conn_id);
			if(_async_obj_get_state(loop, async) == _ST_WAIT_DATA &&
			   _async_obj_set_state(loop, async, _ST_READY) == ERROR_CODE(int))
			    LOG_WARNING("cannot set the connection object %"PRIu32" to ready state", current->conn_id);
			async->rdy_posted = 0;
		}
		else if(current->type == _MT_END)
		{
			LOG_DEBUG("QM: connection released notification on connection object %"PRIu32, current->conn_id);
			if(async->data_end) LOG_WARNING("connection object %"PRIu32" has been released twice!", current->conn_id);
			async->data_end = 1;
			_async_obj_state_t st = _async_obj_get_state(loop, async);
			if(st == _ST_WAIT_DATA || st == _ST_ERROR)
			{
				LOG_DEBUG("connection object %"PRIu32" is currently in %s state, QM %s triggers moving this object to finished list",
				          current->conn_id, _async_obj_state_str[st] ,_message_str[_MT_END]);
				if(ERROR_CODE(int) == _async_obj_set_state(loop, async, _ST_FINISHED))
				    LOG_WARNING("cannot set the connection object %"PRIu32" to FINISHED state", current->conn_id);
			}
		}
		else if(current->type == _MT_CREATE)
		{
			LOG_DEBUG("QM: new async object will be attached to connection object %"PRIu32, current->conn_id);
			if(async->index != ERROR_CODE(uint32_t))
			{
				LOG_WARNING("Ignored async object creation message: async object has been already created for connection object %"PRIu32, current->conn_id);
				continue;
			}
			if(NULL == (async->io_buffer = (char*)mempool_page_alloc()))
			{
				LOG_WARNING("Cannot allocate memory for the IO buffer");
				continue;
			}

			async->b_begin = async->b_end = 0;
			/* We initialize the rdy_posted flag when the message is posted, no need to reinitialize at this point */

			async->index = loop->limits[_NUM_OF_STATES - 1]++;
			loop->st_list[async->index] = current->conn_id;
			if(_async_obj_set_state(loop, async, _ST_WAIT_DATA) == ERROR_CODE(int))
			    LOG_WARNING("cannot set the newly created async object to %s", _async_obj_state_str[_ST_WAIT_DATA]);
		}
		else if(current->type == _MT_KILL) LOG_WARNING("kill message can not be handled at this point");
		else LOG_WARNING("unknown type of queue message");
	}
	if(pthread_mutex_unlock(&loop->q_mutex) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "cannot release the global queue mutex");

	return 0;
}
/**
 * @brief the actual action for the async main loop
 * @param loop the async loop
 * @todo how to kill the async loop properly
 * @return status cdoe
 **/
static inline int _handle_event(module_tcp_async_loop_t* loop)
{
	time_t now = time(NULL);

	int timeout = -1;

	/* Check if we have something to write, we do not want the poll block us */
	if(_get_num_async_in_state(loop, _ST_READY) > 0)
	    timeout = 0;
	else if(_get_num_async_in_state(loop, _ST_WAIT_CONN) > 0)
	{
		/* In this case, even though we do not have any connection becomes ready
		 * the thread needs to wake up and kick out the timed out connections */
		time_t min_ts = loop->objects[loop->st_list[0]].ts;
		if(min_ts + loop->ttl > now)
		    timeout = ((int)(loop->ttl + min_ts - now)) * 1000;
		else
		    timeout = 0;
	}

	LOG_DEBUG("async IO loop is performing poll, timeout: %d ms", timeout);
	int result = os_event_poll_wait(loop->poll, loop->max_events, timeout);

	if(loop->killed)
	{
		LOG_INFO("Async loop gets killed");
		return 0;
	}

	if(result == ERROR_CODE(int))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot finish poll");
	else
	{
		int i;
		for(i = 0; i < result; i ++)
		{
			void* data = os_event_poll_take_result(loop->poll, (size_t)i);
			if(&loop->event_fd == data)
			{
				if(_process_queue_message(loop) == ERROR_CODE(int))
				    LOG_ERROR("Cannot process the queue message");
			}
			else
			{
				/* we have the connection object in ready state */
				_async_obj_t* async = (_async_obj_t*)data;

				if(NULL == async) ERROR_RETURN_LOG(int, "unexpected poll_event data field, code bug!");

				LOG_DEBUG("Connection object %"PRIu32" is ready for write", _async_obj_conn_id(loop, async));

				_async_obj_state_t state = _async_obj_get_state(loop, async);

				if(_ST_WAIT_CONN == state)
				{
					if(_async_obj_set_state(loop, async, _ST_READY) == ERROR_CODE(int))
					    ERROR_RETURN_LOG(int, "Cannot set the state of the async object to _ST_READY");

					if(_async_obj_del_poll(loop, async) == ERROR_CODE(int))
					    ERROR_RETURN_LOG(int, "Cannot remove the async object from poll");

					LOG_DEBUG("Connection object %"PRIu32" has been set to state _ST_READY", _async_obj_conn_id(loop, async));
				}
			}
		}

	}

	/* Kick out all the timed out connections, and put them to raising state */
	for(;loop->limits[_ST_WAIT_CONN] > 0 && loop->objects[loop->st_list[0]].ts + loop->ttl <= now;)
	{
		_async_obj_t* async = _async_obj_get_from_index(loop, 0);

		if(_async_obj_set_state(loop, async, _ST_RAISING) == ERROR_CODE(int))
		{
			LOG_WARNING("Cannot set the timed out connection %"PRIu32" to %s state",
			            _async_obj_conn_id(loop, async),
			            _async_obj_state_str[_ST_RAISING]);
			continue;
		}

		if(_async_obj_del_poll(loop, async) == ERROR_CODE(int))
		{
			LOG_WARNING("Cannot remove the async object from poll");
			continue;
		}

		LOG_DEBUG("Timed out connection %"PRIu32" has been kicked out", _async_obj_conn_id(loop, async));
	}

	/* Process all the async objects */
	if(_process_async_objs(loop) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot process the async objects");

	return 0;
}
static inline void* _async_main(void* arg)
{
	thread_set_name("PbTCPAIO");
	module_tcp_async_loop_t* loop = (module_tcp_async_loop_t*)arg;
	LOG_DEBUG("async loop is started!");
	if(pthread_mutex_lock(&loop->s_mutex) < 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("cannot acquire the startup mutex");

	loop->started = 1;
	if(pthread_cond_signal(&loop->s_cond) < 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("cannot notify the scheduler thread");

	if(pthread_mutex_unlock(&loop->s_mutex) < 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("cannot release the startup mutex");


	for(;!loop->killed;)
	    if(_handle_event(loop) == ERROR_CODE(int))
	        LOG_ERROR("Cannot handle the event");

	LOG_INFO("Exiting async loop");

	return NULL;
}

module_tcp_async_loop_t* module_tcp_async_loop_new(uint32_t pool_size, uint32_t event_size, time_t ttl, ssize_t (*write)(int, const void*, size_t))
{
	uint32_t i, tmp, size;
	module_tcp_async_loop_t* ret = (module_tcp_async_loop_t*)calloc(1, sizeof(module_tcp_async_loop_t));

	if(NULL == ret) ERROR_LOG_ERRNO_GOTO(ERR, "cannot allocate memory for the async loop");

	ret->capacity = pool_size;
	ret->max_events = event_size;
	ret->write = write;
	ret->ttl = ttl;

	if(NULL == (ret->objects = (_async_obj_t*)malloc(sizeof(_async_obj_t) * ret->capacity)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot allocate memory for the async object array");
	for(i = 0; i < ret->capacity; i ++)
	    ret->objects[i].index = ERROR_CODE(uint32_t);

	if(NULL == (ret->st_list = (uint32_t*)malloc(sizeof(uint32_t) * ret->capacity)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot allocate memory for the state list");

	tmp = pool_size * _NUM_CONN_MSG_TYPS + 1;
	size = 1;
	for(;tmp > 1; tmp >>= 1,size <<= 1);
	if(pool_size * _NUM_CONN_MSG_TYPS + 1 > size) size <<= 1;
	ret->q_mask = size - 1;
	ret->q_front = ret->q_rear = 0;
	LOG_DEBUG("There are %"PRIu32" elements in the message qeueue", size);
	if(NULL == (ret->queue = (_message_t*)malloc(sizeof(_message_t) * size)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot allocate memory for the message queue");

	if((ret->poll = os_event_poll_new()) == NULL)
	    ERROR_LOG_GOTO(ERR, "cannot create poll object");

	if(pthread_mutex_init(&ret->q_mutex, NULL) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot initialize the queue mutex");
	ret->i_q_mutex = 1;

	if(pthread_mutex_init(&ret->s_mutex, NULL) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot initialize the startup mutex");
	ret->i_s_mutex = 1;

	if(pthread_cond_init(&ret->s_cond, NULL) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot initialize the startup cond var");
	ret->i_s_cond = 1;

	os_event_desc_t event = {
		.type = OS_EVENT_TYPE_USER,
		.user = {
			.data = &ret->event_fd
		}
	};

	if(ERROR_CODE(int) == (ret->event_fd = os_event_poll_add(ret->poll, &event)))
	    ERROR_LOG_GOTO(ERR, "Cnnot add the user event to poll wait list");

	/* Finally, start the loop */
	if(NULL == (ret->loop = thread_new(_async_main, ret, THREAD_TYPE_IO)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot spawn the async loop thread");

	if(pthread_mutex_lock(&ret->s_mutex) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot acquire the startup mutex");

	while(!ret->started)
	    if(pthread_cond_wait(&ret->s_cond, &ret->s_mutex) < 0)
	        LOG_ERROR_ERRNO("cannot performe pthread wait");

	if(pthread_mutex_unlock(&ret->s_mutex) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot acquire the startup mutex");

	return ret;
ERR:
	if(NULL != ret)
	{
		if(ret->objects != NULL) free(ret->objects);
		if(ret->st_list != NULL) free(ret->st_list);
		if(ret->queue != NULL) free(ret->queue);
		if(ret->poll != NULL) os_event_poll_free(ret->poll);
		if(ret->event_fd > 0) close(ret->event_fd);

		if(ret->i_q_mutex) pthread_mutex_destroy(&ret->q_mutex);
		if(ret->i_s_mutex) pthread_mutex_destroy(&ret->s_mutex);
		if(ret->i_s_cond)  pthread_cond_destroy(&ret->s_cond);

		if(ret->loop != NULL) thread_free(ret->loop, NULL);

		free(ret);
	}
	return NULL;
}

/**
 * @brief post a message to the event queue
 * @param loop the event loop
 * @param type the type of this message
 * @param conn_id the connection id
 * @return status code
 **/
static inline int _post_message(module_tcp_async_loop_t* loop, _message_type_t type, uint32_t conn_id)
{
	if(pthread_mutex_lock(&loop->q_mutex) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot acquire the global queue mutex");

	if(type == _MT_CREATE)
	{
		/* when a _MT_CREATE message is posted, it means we do not have anything about
		 * this connection object in the queue. In addition, the async object is not
		 * initialized for this time.
		 * So we should initialize the rdy_posted bit at this point eventhough it's not
		 * yet processed, because it's possible for a _MT_READY message posted after this
		 * message */
		loop->objects[conn_id].rdy_posted = 0;
	}

	if(type != _MT_READY || !loop->objects[conn_id].rdy_posted)
	{
		_message_t* msg = loop->queue + (loop->q_rear & loop->q_mask);

		msg->type = type;
		msg->conn_id = conn_id;

		LOG_DEBUG("Posted QM @ %"PRIu32": <type=%s, conn=%"PRIu32">", loop->q_rear, _message_str[type], conn_id);
		loop->q_rear ++;

		if(type == _MT_READY)
		    loop->objects[conn_id].rdy_posted = 1;
	}
	else LOG_DEBUG("Ignored duplicate %s message on connection object %"PRIu32, _message_str[type], conn_id);

	if(pthread_mutex_unlock(&loop->q_mutex) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot release the global queue mutex");


	uint64_t val = 1;
	if(write(loop->event_fd, &val, sizeof(uint64_t)) > 0)
	    return 0;
	else
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot write to the event fd");

ERR:
	return ERROR_CODE(int);
}

int module_tcp_async_write_register(module_tcp_async_loop_t* loop,
                                    uint32_t conn_id, int fd, size_t buf_size,
                                    module_tcp_async_write_data_func_t get_data,
                                    module_tcp_async_write_cleanup_func_t cleanup,
                                    module_tcp_async_write_error_func_t on_error,
                                    void* handle)
{
	if(NULL == loop || conn_id >= loop->capacity || fd < 0 || get_data == NULL || cleanup == NULL || on_error == NULL || handle == NULL)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(loop->objects[conn_id].index != ERROR_CODE(uint32_t))
	    ERROR_RETURN_LOG(int, "the connection object %"PRIu32" has undergoing async operation", conn_id);

	/* This is safe, because unless the async object released the connection object, it cannot
	 * be popped up next time. So at this point, there must be no pending queue message for this
	 * connection, since for each async write operation, data_end must be the last queue message */
	loop->objects[conn_id].fd = fd;
	loop->objects[conn_id].get_data = get_data;
	loop->objects[conn_id].cleanup = cleanup;
	loop->objects[conn_id].onerror = on_error;
	loop->objects[conn_id].handle = handle;
	loop->objects[conn_id].data_end = 0;
	loop->objects[conn_id].b_size = buf_size;

	if(loop->objects[conn_id].b_size > (uint32_t)getpagesize())
	{
		LOG_WARNING("Adjusted the buffer size to fit one page");
	    loop->objects[conn_id].b_size = (uint32_t)getpagesize();
	}

	LOG_INFO("Initialized async operation on connection object %"PRIu32, conn_id);

	return _post_message(loop, _MT_CREATE, conn_id);
}

int module_tcp_async_write_data_ends(module_tcp_async_loop_t* loop, uint32_t conn_id)
{
	if(NULL == loop || conn_id >= loop->capacity)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return _post_message(loop, _MT_END, conn_id);
}

int module_tcp_async_write_data_ready(module_tcp_async_loop_t* loop, uint32_t conn_id)
{
	if(NULL == loop || conn_id >= loop->capacity)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return _post_message(loop, _MT_READY, conn_id);
}


void* module_tcp_async_get_data_handle(module_tcp_async_loop_t* loop, uint32_t conn_id)
{
	if(NULL == loop || conn_id >= loop->capacity)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	return loop->objects[conn_id].handle;
}

int module_tcp_async_loop_free(module_tcp_async_loop_t* loop)
{
	if(NULL == loop) ERROR_RETURN_LOG(int, "Invalid arguments");
	int rc = 0;
	/* If the loop is started, we should kill the loop first */
	if(loop->started)
	{
		loop->killed = 1;
		BARRIER();
		if(ERROR_CODE(int) == _post_message(loop, _MT_KILL, 0))
		    ERROR_RETURN_LOG(int, "Cannot send kill message to the async loop");

		if(thread_free(loop->loop, NULL) == ERROR_CODE(int))
		{
			LOG_ERROR_ERRNO("Cannot join the loop");
			rc = ERROR_CODE(int);
		}
		else
		    LOG_DEBUG("Async loop is stopped");

		uint32_t i;
		for(i = 0; i < loop->capacity; i ++)
		{
			if(loop->objects[i].index == ERROR_CODE(uint32_t)) continue;

			if(ERROR_CODE(int) == loop->objects[i].cleanup(i, loop))
			{
				LOG_ERROR("Cannot invoke the cleanup handler");
				rc = ERROR_CODE(int);
			}

			//free(loop->objects[i].io_buffer);
			if(mempool_page_dealloc(loop->objects[i].io_buffer) == ERROR_CODE(int))
			{
				LOG_ERROR("Cannot dealloc the iobuffer");
				rc = ERROR_CODE(int);
			}
		}
	}

	if(NULL != loop->objects) free(loop->objects);
	if(NULL != loop->st_list) free(loop->st_list);
	if(NULL != loop->queue) free(loop->queue);
	if(NULL != loop->poll && ERROR_CODE(int) == os_event_poll_free(loop->poll))
	    rc = ERROR_CODE(int);

	if(loop->event_fd >= 0) close(loop->event_fd);
	if(loop->i_q_mutex && pthread_mutex_destroy(&loop->q_mutex) < 0)
	{
		LOG_ERROR_ERRNO("Cannot dispose the global queue mutex");
		rc = ERROR_CODE(int);
	}

	if(loop->i_s_mutex && pthread_mutex_destroy(&loop->s_mutex) < 0)
	{
		LOG_ERROR_ERRNO("Cannot dispose the startup mutex");
		rc = ERROR_CODE(int);
	}

	if(loop->i_s_cond && pthread_cond_destroy(&loop->s_cond) < 0)
	{
		LOG_ERROR_ERRNO("Cannot dispose the startup cond variable");
		rc = ERROR_CODE(int);
	}

	free(loop);

	return rc;
}
