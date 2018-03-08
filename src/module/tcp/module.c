/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>

#include <barrier.h>
#include <error.h>
#include <predict.h>
#include <utils/log.h>
#include <utils/static_assertion.h>
#include <utils/mempool/objpool.h>
#include <utils/mempool/page.h>

#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/modtab.h>

#include <module/tcp/module.h>
#include <module/tcp/pool.h>
#include <module/tcp/async.h>

/**
 * @brief the macro for the flag that represents a data source callback page
 **/
#define _DATA_SOURCE_CALLBACK ((uint32_t)~0)

/**
 * @brief the pipe direction
 **/
typedef enum {
	_DIR_IN = 0,   /*!< input pipe */
	_DIR_OUT = 1   /*!< output pipe */
} _pipe_dir_t;

/**
 * @brief previous declearation for the dispose state function
 * @param data the state data
 * @return status code
 **/
static int _dispose_state(void* data);

/**
 * @brief the state used to attach to the connection pool object
 **/
typedef struct {
	size_t                          total_bytes;            /*!< the total number of bytes in the buffer */
	size_t                          unread_bytes;           /*!< the unread bytes */
	void*                           user_space_data;        /*!< the user space data attached to this connection pool object */
	uint32_t                        buffer_exposed:1;       /*!< Indicates if we have a buffer exposed */
	uint32_t                        user_state_pending:1;   /*!< indicates if we have user space data pending to push */
	itc_module_state_dispose_func_t disp;                   /*!< the dispose function for the case the connection object must be killed */
	uintpad_t __padding__[0];
	char                            buffer[0];              /*!< the read buffer */
} _state_t;
STATIC_ASSERTION_SIZE(_state_t, buffer, 0);
STATIC_ASSERTION_LAST(_state_t, buffer);

/**
 * @brief the internal async write data buffer page
 **/
typedef struct _async_buf_page_t {
	struct _async_buf_page_t*     next;             /*!< the next buffer */
	union {
		uint32_t                  nbytes;           /*!< the size of the data section */
		uint32_t                  callback;         /*!< if this is actually a data source callback rather than the real buffer, if this is true, it should be ~0 */
	};
	union {
		char                      data[0];          /*!< the actual buffer */
		itc_module_data_source_t  data_source[0];   /*!< the data source buffer, valid only if callback == (uintn32_t)~0u */
	};
} __attribute__((packed)) _async_buf_page_t;
STATIC_ASSERTION_LAST(_async_buf_page_t, data);
STATIC_ASSERTION_SIZE(_async_buf_page_t, data, 0);

/**
 * @brief the previous declearation for a pipe handle
 **/
typedef struct _handle_t _handle_t;

/**
 * @brief the internal async write data handle
 **/
typedef struct {
	module_tcp_pool_t* conn_pool;      /*!< the connection pool where this handle belongs to */
	uint32_t           page_off;       /*!< the page offset */
	_async_buf_page_t* page_begin;     /*!< the data buffer begin */
	_async_buf_page_t* page_end;       /*!< the data buffer end */
	int                release_mode;   /*!< the release mode */
	_state_t*          release_data;   /*!< the data to release */
	int                error;          /*!< if the handle is in an error state */
	pthread_mutex_t*   mutex;          /*!< the async mutex used by this async handle */
} _async_handle_t;

/**
 * @brief the internal pipe handle type
 **/
struct _handle_t {
	uint32_t                        has_more:1;          /*!< if it's *possible* to have more data in the pipe */
	_pipe_dir_t                     dir:1;               /*!< in which direction */
	int                             fd;                  /*!< the file descriptor */
	uint32_t                        idx;                 /*!< the connection object index */
	size_t                          last_read_offset;    /*!< the offset of the begining of the last read */
	itc_module_state_dispose_func_t disp;                /*!< how to dispose the user-space data */
	_state_t*                       state;               /*!< only used on inputs, but the reason why output pipe gets a copy is the output pipe may deallocate later */
	_async_handle_t*                async_handle;        /*!< only used on outputs, the undergoing async write object handle */
};

/**
 * @brief the shared async buffer mutex, this thread local for each scheduler
 **/
__thread struct {
	int             created;   /*!< if the mutex is created */
	pthread_mutex_t mutex;     /*!< the actual mutex */
} _async_mutex;

/**
 * @brief the context for a TCP module context
 **/
typedef struct {
	module_tcp_pool_configure_t pool_conf;            /*!< the TCP pool configuration */
	int                         retry_interval;       /*!< When the TCP pool cannot be configured, how much time we want to sleep before retry */
	int                         pool_initialized;     /*!< indicates if the pool has been initialized */
	int                         sync_write_attempt;   /*!< If do a synchronized write attempt before initialize a async operation */
	int                         slave_mode;           /*!< The slave working mode, which means the module should not start the event loop */
	int                         fork_id;              /*!< The id used to identify the TCP module instance that listen to the same port */
	uint32_t                    async_buf_size;       /*!< The size of the async write buffer */
	module_tcp_pool_t*          conn_pool;            /*!< The TCP connection pool object */
	module_tcp_async_loop_t*    async_loop;           /*!< The async loop for this TCP module instance */
} _module_context_t;

/**
 * @brief the size of a OS page
 **/
static uint32_t _pagesize;

/** @brief the memory pool used to allocate async handle */
static mempool_objpool_t* _async_handle_pool = NULL;

/** @brief the memory pool used to allocate the async page object which represents a data source object */
static mempool_objpool_t* _async_data_source_pool = NULL;

/** @brief the counter indicates how many instances is initialized */
static uint32_t _instance_count = 0;

/**
 * @brief dispose a user defined state
 * @param state the state data
 * @return status code
 **/
static inline int _dispose_user_state(_state_t* state)
{
	int rc = 0;
	if(state->disp != NULL && state->user_space_data != NULL)
	    rc = state->disp(state->user_space_data);
	else if(state->disp == NULL && state->user_space_data != NULL)
	{
		LOG_WARNING("there's no dispose function for the user-space state, memory leak is possible");
		rc = ERROR_CODE(int);
	}
	return rc;
}

/**
 * @brief dispose a connection state
 * @param data the connection state to dispose
 * @return status code
 **/
static int _dispose_state(void* data)
{
	_state_t* state = (_state_t*)data;
	int rc = _dispose_user_state(state);
	//free(data);
	if(ERROR_CODE(int) == mempool_page_dealloc(data))
	    rc = ERROR_CODE(int);
	return rc;
}


/**
 * @brief create a new async buffer page
 * @return the newly created async buffer page, NULL on error cases
 **/
static inline _async_buf_page_t* _async_buf_data_page_new(void)
{
	_async_buf_page_t* ret = (_async_buf_page_t*)mempool_page_alloc();
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for new async write page");
	ret->next = NULL;
	ret->nbytes = 0;
	return ret;
}

/**
 * @brief create a new data source page for the given data source
 * @param data_source the data source we need to create the page for
 * @return the newly created page, NULL on error
 **/
static inline _async_buf_page_t* _async_buf_data_source_page_new(const itc_module_data_source_t data_source)
{
	_async_buf_page_t* ret = (_async_buf_page_t*)mempool_objpool_alloc(_async_data_source_pool);
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the data source page");

	ret->next = NULL;
	ret->callback = _DATA_SOURCE_CALLBACK;
	*ret->data_source = data_source;

	return ret;
}

/**
 * @brief create a new async handle
 * @return the newly created async handle, NULL on error case
 */
static inline _async_handle_t* _async_handle_new(_module_context_t* ctx)
{
	_async_handle_t* ret = (_async_handle_t*)mempool_objpool_alloc(_async_handle_pool);
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("cannot allocate memory for new async data handle");

	ret->page_off = 0;
	ret->page_begin = ret->page_end = NULL;
	ret->release_mode = -1;
	ret->release_data = NULL;
	ret->mutex = &_async_mutex.mutex;
	ret->error = 0;
	ret->conn_pool = ctx->conn_pool;

	/* Because this is thread local, so no race condition possible at this point */
	if(_async_mutex.created == 0)
	{
		LOG_DEBUG("the scheduler thread has no async mutex, creating one");
		if((errno = pthread_mutex_init(&_async_mutex.mutex, NULL)) != 0)
		{
			mempool_objpool_dealloc(_async_handle_pool, ret);
			ERROR_PTR_RETURN_LOG_ERRNO("cannot create the mutex for the scheduler thread");
		}
		_async_mutex.created = 1;
	}

	return ret;
}

/**
 * @brief check if the async buf page is actually a data source callback rather than a real data buffer page
 * @param page the page to check
 * @return the check result
 **/
static inline int _async_buf_page_is_data_source(const _async_buf_page_t* page)
{
	return page->callback == _DATA_SOURCE_CALLBACK;
}

/**
 * @brief dispose  a used async buffer page
 * @param page the page to dispose
 * @note this function can dispose both the data buffer and the data source object
 * @return status code
 **/
static inline int _async_buf_page_free(_async_buf_page_t* page)
{
	if(_async_buf_page_is_data_source(page))
	{
		int rc = page->data_source->close(page->data_source->data_handle);

		if(ERROR_CODE(int) == rc)
		    LOG_ERROR("Cannot close the data source object properly");

		if(ERROR_CODE(int) == mempool_objpool_dealloc(_async_data_source_pool, page))
		{
			LOG_ERROR("Cannot deallocate the async data source page");
			rc = ERROR_CODE(int);
		}

		return rc;
	}
	else
	    return mempool_page_dealloc(page);
}

/**
 * @brief the data source callback for the async handle
 * @param conn the connection id
 * @param data the data buffer
 * @param size the buf size
 * @param loop the async loop called this function
 * @return number of bytes has been written
 **/
static inline size_t _async_handle_getdata(uint32_t conn, void* data, size_t size, module_tcp_async_loop_t* loop)
{
	_async_handle_t* handle = (_async_handle_t*)module_tcp_async_get_data_handle(loop, conn);

	if(NULL == handle)
	    ERROR_RETURN_LOG_ERRNO(size_t, "cannot get the data handle for connection object %"PRIu32, conn);

	size_t ret = 0;
	char*  buf = (char*)data;

	if((errno = pthread_mutex_lock(handle->mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(size_t, "cannot acquire the async handle mutex");

	for(;handle->page_begin != NULL && size > 0;)
	{
		if(_async_buf_page_is_data_source(handle->page_begin))
		{
			int eos_rc = handle->page_begin->data_source->eos(handle->page_begin->data_source->data_handle);

			if(ERROR_CODE(int) == eos_rc)
			{
				LOG_WARNING("The data source page will be ignored beause eos call returns an error");
				goto PAGE_EXHAUSTED;
			}

			if(eos_rc)
			{
				LOG_DEBUG("The data source page has been exhausted, moving to the next buffer page");
				goto PAGE_EXHAUSTED;
			}

			itc_module_data_source_event_t event;

			size_t bytes_read = handle->page_begin->data_source->read(handle->page_begin->data_source->data_handle, buf, size, &event);
			if(ERROR_CODE(size_t) == bytes_read || bytes_read > size)
			{
				LOG_WARNING("The data source page will be ignored because the read call returns an error");
				goto PAGE_EXHAUSTED;
			}

			if(bytes_read == 0)
			{
				LOG_DEBUG("The data source callback function returns empty, try to register the event");

				if(event.fd >= 0)
				{
					if(ERROR_CODE(int) == module_tcp_async_set_data_event(loop, conn, event))
					{
						LOG_WARNING("Cannot setup the event source FD for the connection, transmission may be delayed");
					}
				}

				break;
			}

			ret += bytes_read;
			size -= bytes_read;
			buf += bytes_read;
		}
		else
		{
			/* Previously, if the next page is empty, which means we have reached the end of the buffer.
			 * However, when we introduces the data source callback change, this is no longer true, because
			 * a callback page can not reuse this memory. Which means the page to reused is dead in this case.
			 *
			 * If this is the last page, we don't need to run exhausted page code at this point
			 * The reason why we do not execute the exhausted page code at this time is because
			 * if the nbytes of the page is 0, it means this is the last page, and it's alread empty,
			 * so there's no effect even if we run the exhuated page code.
			 *
			 * If this is not the last page, we should deallocate the page. in this case we should run page exhausted
			 **/
			if(handle->page_begin->nbytes <= 0)
			{
				if(handle->page_begin->next == NULL)
				    break;
				else
				    goto PAGE_EXHAUSTED;
			}


			uint32_t bytes_to_read = handle->page_begin->nbytes - handle->page_off;

			if(bytes_to_read > size) bytes_to_read = (uint32_t)size;

			memcpy(buf, handle->page_begin->data + handle->page_off, bytes_to_read);

			handle->page_off += bytes_to_read;
			ret += bytes_to_read;
			size -= bytes_to_read;
			buf += bytes_to_read;

			if(handle->page_off >= handle->page_begin->nbytes)
			    goto PAGE_EXHAUSTED;
		}
		continue;

PAGE_EXHAUSTED:
		/* We should try to reuse the page, however, if the page is either the non-last one, or callback page,
		 * we will not be able to reuse it */
		if(handle->page_begin->next != NULL || _async_buf_page_is_data_source(handle->page_begin))
		{
			/* because this is not the last page, so we can not reuse the page */
			_async_buf_page_t* tmp = handle->page_begin;

			handle->page_off = 0;
			handle->page_begin = handle->page_begin->next;
			if(ERROR_CODE(int) == module_tcp_async_clear_data_event(loop, conn))
			    LOG_WARNING("Cannot clear the data event");
			if(ERROR_CODE(int) == _async_buf_page_free(tmp))
			    LOG_WARNING("Cannot deallocate the async buffer page");

			if(handle->page_end == tmp) handle->page_end = NULL;

			LOG_DEBUG("data page disposed");
		}
		else
		{
			handle->page_off = 0;
			handle->page_begin->nbytes = 0;
			LOG_DEBUG("reused the last data page");
		}
	}

	if((errno = pthread_mutex_unlock(handle->mutex)) != 0)
	    ERROR_RETURN_LOG_ERRNO(size_t, "cannot acquire the async handle mutex");

	return ret;
}

/**
 * @brief the callback function called when the async object is entering an error state
 * @param conn the connection id
 * @param loop the async loop called this function
 * @return status code
 **/
static inline int _async_handle_onerror(uint32_t conn, module_tcp_async_loop_t* loop)
{
	_async_handle_t* handle = (_async_handle_t*)module_tcp_async_get_data_handle(loop, conn);

	if(NULL == handle)
	    ERROR_RETURN_LOG(int, "cannot get the data handle for the connection object %"PRIu32, conn);

	handle->error = 1;

	LOG_INFO("Connection object %"PRIu32" has been set to an error state", conn);

	return 0;
}

/**
 * @brief Check if the async handle is currently empty
 * @param conn The connection ID
 * @param loop The async loop
 * @return check rsult or status code
 * @note This function will called only after the data_end message has been sent,
 *       thus we don't need to care about the thread safety.
 **/
static inline int _async_handle_empty(uint32_t conn, module_tcp_async_loop_t* loop)
{
	_async_handle_t* handle = (_async_handle_t*)module_tcp_async_get_data_handle(loop, conn);

	if(NULL == handle)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot get the data handle for connection object %"PRIu32, conn);

	if(handle->page_begin == NULL) return 1;

	if(handle->page_begin->next == NULL && !_async_buf_page_is_data_source(handle->page_begin))
	    return handle->page_begin->nbytes <= handle->page_off;

	return 0;
}

/**
 * @brief the callback function called when the async object is going to be disposed
 * @param conn the connection id
 * @param loop the async loop called this function
 * @return status code
 **/
static inline int _async_handle_dispose(uint32_t conn, module_tcp_async_loop_t* loop)
{
	int rc = 0;
	_async_handle_t* handle = (_async_handle_t*)module_tcp_async_get_data_handle(loop, conn);

	if(NULL == handle)
	    ERROR_RETURN_LOG_ERRNO(int, "cannot get the data handle for the connection object %"PRIu32, conn);

	if(!handle->error)
	{
		if(handle->release_mode != -1 && ERROR_CODE(int) == module_tcp_pool_connection_release(handle->conn_pool, conn, handle->release_data, handle->release_mode))
		{
			LOG_ERROR("Cannot release the connection");
			rc = ERROR_CODE(int);
		}
	}
	else
	{
		if(handle->release_data != NULL) _dispose_state(handle->release_data);

		if(ERROR_CODE(int) == module_tcp_pool_connection_release(handle->conn_pool, conn, NULL, MODULE_TCP_POOL_RELEASE_MODE_PURGE))
		{
			LOG_ERROR("Cannot close the connection object %"PRIu32" which is in error state", conn);
			rc = ERROR_CODE(int);
		}
	}

	_async_buf_page_t* tmp;
	for(;handle->page_begin;)
	{
		tmp = handle->page_begin;
		handle->page_begin = handle->page_begin->next;
		if(ERROR_CODE(int) == module_tcp_async_clear_data_event(loop, conn))
		{
			LOG_ERROR("Cannot clear the data event");
			rc = ERROR_CODE(int);
		}
		if(ERROR_CODE(int) == _async_buf_page_free(tmp))
		{
			LOG_ERROR("Cannot dispoase the async buffer page");
			rc = ERROR_CODE(int);
		}
	}

	if(ERROR_CODE(int) == mempool_objpool_dealloc(_async_handle_pool, handle))
	{
		LOG_ERROR("Cannot dispose the async handle object for connection object %"PRIu32, conn);
		rc = ERROR_CODE(int);
	}

	LOG_DEBUG("Async write data handle for connection object %"PRIu32" has been disposed", conn);
	return rc;
}

static int _cleanup(void* __restrict ctx)
{
	_module_context_t* context = (_module_context_t*)ctx;

	int rc = 0;
	if(_instance_count == 1)
	{
		if(NULL != _async_handle_pool && ERROR_CODE(int) == mempool_objpool_free(_async_handle_pool))
		{
			LOG_ERROR("Cannot dispose the async handle object pool");
			rc = ERROR_CODE(int);
		}

		if(NULL != _async_data_source_pool && ERROR_CODE(int) == mempool_objpool_free(_async_data_source_pool))
		{
			LOG_ERROR("Cannot dispose the async data source object pool");
			rc = ERROR_CODE(int);
		}
	}

	if(context->async_loop != NULL && module_tcp_async_loop_free(context->async_loop) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot finalize the async loop");
		rc = ERROR_CODE(int);
	}

	if(module_tcp_pool_free(context->conn_pool) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot finalize the connection pool");
		rc = ERROR_CODE(int);
	}

	_instance_count --;
	return rc;
}
static inline int _init_connection_pool(_module_context_t* __restrict context)
{
	if(!context->pool_initialized && module_tcp_pool_configure(context->conn_pool, &context->pool_conf) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot configure the connection pool, retry after %d seconds", context->retry_interval);
		usleep((uint32_t)(context->retry_interval * 1000000));
		if(context->retry_interval < 30)
		    context->retry_interval *= 2;
		return ERROR_CODE(int);
	}

	if(!context->pool_initialized)
	    LOG_DEBUG("TCP Connection pool has been successfully initinalized");

	context->pool_initialized = 1;
	return 0;
}

static inline int _module_context_init(_module_context_t* ctx, _module_context_t* master)
{
	if(master != NULL &&  master->fork_id != 0)
	    ERROR_RETURN_LOG(int, "Invalid arguments: Trying start multithreaded event loop on a forked module?");

	if(_instance_count == 0)
	{
		if(NULL == (_async_handle_pool = mempool_objpool_new(sizeof(_async_handle_t))))
		    ERROR_RETURN_LOG(int, "Cannot create async handle object pool");

		if(NULL == (_async_data_source_pool = mempool_objpool_new(sizeof(_async_buf_page_t) + sizeof(itc_module_data_source_t))))
		    ERROR_RETURN_LOG(int, "Cannot create async data source object pool");

		int pagesize = getpagesize();
		if(pagesize < 0) ERROR_RETURN_LOG_ERRNO(int, "Cannot get page size");

		_pagesize = (uint32_t)pagesize;
	}


	ctx->sync_write_attempt = 1;
	ctx->async_buf_size     = MODULE_TCP_MAX_ASYNC_BUF_SIZE;

	if(ctx->async_buf_size > (uint32_t)getpagesize())
	    ctx->async_buf_size = (uint32_t)getpagesize();

	ctx->pool_initialized = 0;

	ctx->async_loop = NULL;

	if(NULL == master)
	{
		if(NULL == (ctx->conn_pool = module_tcp_pool_new()))
		    ERROR_RETURN_LOG(int, "Cannot create TCP connection pool");
		ctx->fork_id = 0;
	}
	else
	{
		if(ERROR_CODE(int) == (ctx->fork_id = module_tcp_pool_num_forks(master->conn_pool)))
		    ERROR_RETURN_LOG(int, "Cannot get new port ID");

		ctx->fork_id ++;

		if(NULL == (ctx->conn_pool = module_tcp_pool_fork(master->conn_pool)))
		    ERROR_RETURN_LOG(int, "Cannot fork TCP connection pool");

		ctx->pool_conf.port = master->pool_conf.port;

	}

	_instance_count ++;
	return 0;
}

static int _init(void* __restrict ctx, uint32_t argc, char const* __restrict const* __restrict argv)
{
	_module_context_t* context = (_module_context_t*)ctx;
	if(NULL == context)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	context->pool_conf.port         = 8888;
	context->pool_conf.bind_addr    = "0.0.0.0";
	context->pool_conf.size         = 65536;
	context->pool_conf.ttl          = 240;
	context->pool_conf.event_size   = 64;
	context->pool_conf.min_timeout  = 1;
	context->pool_conf.tcp_backlog  = 512;
	context->pool_conf.reuseaddr    = 0;
	context->pool_conf.ipv6         = 0;
	context->pool_conf.accept_retry_interval = 5;
	context->pool_conf.dispose_data = _dispose_state;
	context->slave_mode = 0;
	context->retry_interval = 1;

	uint32_t i = 0;
	if(argc > 0)
	{
		if(strcmp(argv[0], "--slave") == 0)
		{
			context->slave_mode = 1;
			i = 1;
		}
	}

	_module_context_t* master = NULL;

	if(argc - i == 1)
	{
		if(argv[i][0] < '0' || argv[i][0] > '9')
		{
			uint32_t j;
			for(j = 0; module_tcp_module_def.mod_prefix[j] && module_tcp_module_def.mod_prefix[j] == argv[i][j]; j ++);
			if(module_tcp_module_def.mod_prefix[j] != 0 || argv[i][j] != '.')
			    ERROR_RETURN_LOG(int, "Invalid arguments: Not a TCP module: %s", argv[i]);

			/* We need to share the port */
			itc_module_type_t master_type = itc_modtab_get_module_type_from_path(argv[i]);

			if(ERROR_CODE(itc_module_type_t) == master_type)
			    ERROR_RETURN_LOG(int, "Invalid arguments: TCP module %s not exists", argv[i]);

			const itc_modtab_instance_t* master_mod_inst = itc_modtab_get_from_module_type(master_type);
			if(NULL == master_mod_inst)
			    ERROR_RETURN_LOG(int, "Cannnot get the master type %u", master_type);

			master = (_module_context_t*)master_mod_inst->context;
		}
		else
		{
			/* This is the master event loop */
			uint16_t port = (uint16_t)atoi(argv[i]); /* The first argument */
			context->pool_conf.port = port;
		}
	}

	if(ERROR_CODE(int) == _module_context_init(context, master))
	    ERROR_RETURN_LOG(int, "Cannot initialize the TCP module context");

	return 0;
}

static int _accept(void* __restrict ctx, const void* __restrict args, void* __restrict inbuf, void* __restrict outbuf)
{
	(void) args;

	_module_context_t* context = (_module_context_t*)ctx;

	if(NULL == context) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(_init_connection_pool(context) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot configure the connection pool");

	_handle_t* in = (_handle_t*)inbuf;
	_handle_t* out = (_handle_t*)outbuf;

	in->dir = _DIR_IN;
	out->dir = _DIR_OUT;

	module_tcp_pool_conninfo_t conn;

	if(module_tcp_pool_connection_get(context->conn_pool, &conn) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot get active request form tne connection pool");
		return ERROR_CODE(int);
	}

	_state_t* stat;

	if(conn.data != NULL)
	{
		LOG_DEBUG("This connection has previously saved state, pop it");
		stat = in->state = out->state = (_state_t*)conn.data;
	}
	else
	{
		LOG_DEBUG("This connection don't state variable, allocating one");
		if(NULL == (stat = in->state = out->state = (_state_t*)mempool_page_alloc()))
		{
			LOG_ERROR("cannot allocate memory for the handle state");
			module_tcp_pool_connection_release(context->conn_pool, conn.idx, NULL, MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_READ);
			return ERROR_CODE(int);
		}

		stat->total_bytes = 0;
		stat->unread_bytes = 0;
		stat->user_space_data = NULL;
		stat->disp = NULL;
	}

	in->disp = out->disp = stat->disp;
	in->has_more = 1;
	stat->user_state_pending = 0;
	stat->buffer_exposed = 0;
	in->fd = out->fd = conn.fd;
	in->idx = out->idx = conn.idx;
	in->async_handle = out->async_handle = NULL;

	return 0;
}
static int  _read_to_buffer(_handle_t* handle)
{
	if(handle->state->unread_bytes == 0)
	{
		LOG_DEBUG("The buffer is empty, read from scoket");
		size_t nbytes = _pagesize - sizeof(_state_t);
		ssize_t rc = read(handle->fd, handle->state->buffer, nbytes);
		if(rc < 0)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
				LOG_DEBUG("The socket is waiting for more data");
				handle->state->unread_bytes = 0;
				handle->state->total_bytes = 0;
				return 0;
			}
			else
			{
				if(errno != ECONNRESET)
				    LOG_ERROR_ERRNO("Socket error");
				else
				    LOG_TRACE_ERRNO("Socket error");
				return ERROR_CODE(int);
			}
		}
		else if(rc == 0)
		{
			handle->has_more = 0;
		}
		else
		{
			handle->state->unread_bytes = (size_t)rc;
			handle->state->total_bytes = (size_t)rc;
		}
	}

	return 0;
}
static size_t _read(void* __restrict ctx, void* __restrict buffer, size_t bytes_to_read, void* __restrict in)
{
	(void) ctx;
	_handle_t* handle = (_handle_t*)in;
	if(handle->dir != _DIR_IN)
	    ERROR_RETURN_LOG(size_t, "Invalid type of handle, expected read, but get write");

	if(handle->state->buffer_exposed)
	{
		LOG_DEBUG("Since we exposed the read buffer already, thus we are no able to go ahead");
		return 0;
	}

	if(_read_to_buffer(handle) == ERROR_CODE(int)) return 0;

	size_t bytes_from_buffer = bytes_to_read;
	if(handle->state->unread_bytes > 0)
	{
		handle->last_read_offset = handle->state->total_bytes - handle->state->unread_bytes;
		if(bytes_from_buffer > handle->state->unread_bytes)
		    bytes_from_buffer = handle->state->unread_bytes;

		memmove(buffer, ((char*)handle->state->buffer) + handle->state->total_bytes - handle->state->unread_bytes, bytes_from_buffer);

		handle->state->unread_bytes -= bytes_from_buffer;
	}
	else bytes_from_buffer = 0;


	return bytes_from_buffer;
}

static int _get_internal_buf(void* __restrict ctx, void const** __restrict result, size_t* __restrict min, size_t* __restrict max, void* __restrict in)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)in;

	if(NULL == result || NULL == min || NULL == max)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(handle->dir != _DIR_IN)
	    ERROR_RETURN_LOG(int, "Invalid type of handle, expected read, but get write");

	if(handle->state->buffer_exposed)
	{
		LOG_DEBUG("Since we exposed the read buffer already, thus we are no able to go ahead");
		return 0;
	}

	if(_read_to_buffer(handle) == ERROR_CODE(int)) return 0;

	size_t bytes_avaiable = handle->state->unread_bytes;

	if(*min != 0 || *max == 0)
	{
		/* Since the TCP connection might be persistent, it's not clear if the current event
		 * contains that much of data. Thus if min size isn't 0, the buffer could not be returned.
		 * Since the module can not guareentee all the next *min bytes are belongs to current
		 * event */
		LOG_DEBUG("Unable to fetch a buffer with minimal size %zu, returning empty", *min);

		return 0;
	}

	if(bytes_avaiable < *max)
	    *max = bytes_avaiable;

	*result = ((char*)handle->state->buffer) + handle->state->total_bytes - handle->state->unread_bytes;

	handle->state->buffer_exposed = 1;

	return 1;
}

static int _release_internal_buf(void* __restrict ctx, void const* __restrict buf, size_t actual_size, void* __restrict in)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)in;

	if(NULL == buf || actual_size > handle->state->unread_bytes)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(handle->dir != _DIR_IN)
	    ERROR_RETURN_LOG(int, "Invalid type of handle, expected read, but get write");

	if(((const char*)handle->state->buffer) + handle->state->total_bytes - handle->state->unread_bytes != (const char*)buf)
	{
		LOG_DEBUG("Disposing another buffer, thus we just ignore this call");

		return 0;
	}

	handle->state->buffer_exposed = 0;
	handle->state->unread_bytes -= actual_size;

	return 0;
}

/**
 * @brief ensure the async loop is started
 * @param context the context we need to ensure
 * @note this needs to make sure the thread safety, because multiple worker thread may be here
 * @return status code
 **/
static inline int _ensure_async_loop_init(_module_context_t* context)
{
	int rc = 0;

	if(NULL == context->async_loop)
	{
		static volatile uint32_t init = 0;

		while(!__sync_bool_compare_and_swap(&init, 0, 1));

		BARRIER();

		if(NULL == context->async_loop)
		{
			if(NULL == (context->async_loop = module_tcp_async_loop_new(context->pool_conf.size,
			                                                            (uint32_t)context->pool_conf.event_size,
			                                                            context->pool_conf.ttl,
			                                                            /* TODO: make a value for data TTL that make sense  */
			                                                            context->pool_conf.ttl,
			                                                            NULL)))
			{
				rc = ERROR_CODE(int);
				LOG_ERROR("Cannot initialize the async loop");
			}
			else
			    LOG_DEBUG("Async IO loop has been initialized!");
		}

		BARRIER();
		init = 0;
		__sync_synchronize();
	}

	return rc;
}

/**
 * @brief create a new async handle for the given pipe handle
 * @param context the module context
 * @param handle the pipe handle
 * @return status code
 **/
static inline int _create_async_handle(_module_context_t* context, _handle_t* handle)
{
	LOG_DEBUG("Connection object %"PRIu32" has data pending to write, initialize the async object", handle->idx);
	if(NULL == (handle->async_handle = _async_handle_new(context)))
	    ERROR_RETURN_LOG(int, "cannot create async handle for the async object");

	if(module_tcp_async_write_register(context->async_loop, handle->idx, handle->fd, context->async_buf_size,
	                                   _async_handle_getdata, _async_handle_empty, _async_handle_dispose,
	                                   _async_handle_onerror, handle->async_handle) == ERROR_CODE(int))
	{
		mempool_objpool_dealloc(_async_handle_pool, handle->async_handle);
		ERROR_RETURN_LOG(int, "cannot create async object for connection %"PRIu32, handle->idx);
	}

	return 0;
}

/**
 * @brief write a data buffer to the async buf
 * @param context the module context
 * @param handle the module handle
 * @param buf the data buffer to write
 * @param nbytes how many bytes to write
 * @return the number of bytes that has written actually, or error code
 **/
static inline size_t _write_async_buf(_module_context_t* context, _handle_t* handle, const int8_t* buf, size_t nbytes)
{
	if(nbytes == 0) return 0;
	size_t bytes_written = 0;

	LOG_DEBUG("writing %zu bytes to the async buffer", nbytes);
	if((errno = pthread_mutex_lock(handle->async_handle->mutex)) != 0)
	    ERROR_RETURN_LOG(size_t, "cannot acquire the async object mutex for connection %"PRIu32, handle->idx);

	for(;nbytes > 0;)
	{
		if(handle->async_handle->page_end == NULL ||
		   _async_buf_page_is_data_source(handle->async_handle->page_end) ||
		   handle->async_handle->page_end->nbytes == (_pagesize - sizeof(_async_buf_page_t)))
		{
			LOG_DEBUG("no async buffer page is currently available, allocating a new one");
			_async_buf_page_t* page = _async_buf_data_page_new();
			if(NULL == page) ERROR_LOG_GOTO(ASYNC_ERR, "cannot allocate new async buf page");
			if(handle->async_handle->page_end != NULL) handle->async_handle->page_end->next = page;
			handle->async_handle->page_end = page;
			if(NULL == handle->async_handle->page_begin) handle->async_handle->page_begin = page;
		}
		uint32_t copy_size = (uint32_t)(_pagesize - sizeof(_async_buf_page_t)) - handle->async_handle->page_end->nbytes;
		if(copy_size > nbytes) copy_size = (uint32_t)nbytes;

		memcpy(handle->async_handle->page_end->data + handle->async_handle->page_end->nbytes, buf, copy_size);

		handle->async_handle->page_end->nbytes += copy_size;
		bytes_written += copy_size;
		nbytes -= copy_size;
		buf += copy_size;
	}
	goto ASYNC_RET;
ASYNC_ERR:
	bytes_written = ERROR_CODE(size_t);
ASYNC_RET:
	if((errno = pthread_mutex_unlock(handle->async_handle->mutex)) != 0)
	    ERROR_RETURN_LOG(size_t, "cannot release the async object mutex for connection %"PRIu32, handle->idx);
	if(bytes_written != ERROR_CODE(size_t) && module_tcp_async_write_data_ready(context->async_loop, handle->idx) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(size_t, "cannot notify the async IO loop");
	return bytes_written;
}

/**
 * @brief ensure that the async handle is created
 * @param context the module context
 * @param handle the pipe handle
 * @param data the data buffer
 * @param nbytes the data buffer size
 * @param create_anyway indicates we want to create the async handle even if the data buffer is completely written
 * @note the reason why we need this create_anyway param is for the data source version of write, we actually feed this function
 *       with the data buffer contains the leading bytes of the data source stream, even if the data buffer is exhausted, it's possible
 *       the stream is not exhausted, in this case, we need create the async handle anyway. This is the param for this purpose
 * @return the number of bytes that has been written in the sync write attempt, or error code on error cases
 **/
static inline size_t _ensure_async_handle(_module_context_t* context, _handle_t* handle, const int8_t* data, size_t nbytes, int create_anyway)
{
	if(NULL == handle->async_handle)
	{
		LOG_DEBUG("Connection object %"PRIu32" has no corresponding async object", handle->idx);

		ssize_t rc = 0;

		if(context->sync_write_attempt)
		{
			rc = write(handle->fd, data, nbytes);
			if(rc == -1)
			{
				if(errno != EAGAIN && errno != EWOULDBLOCK)
				    ERROR_RETURN_LOG_ERRNO(size_t, "socket error");
				rc = 0;
			}

			nbytes -= (size_t)rc;
		}

		if((nbytes > 0 || create_anyway) && ERROR_CODE(int) == _create_async_handle(context, handle))
		    ERROR_RETURN_LOG(size_t, "Cannot create async handle for the pipe");

		return (size_t)rc;
	}
	else return 0;
}

static int _write_callback(void* __restrict ctx, itc_module_data_source_t data_source, void* __restrict out)
{
	if(NULL == ctx || NULL == out)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(data_source.read == NULL || data_source.eos == NULL || data_source.close == NULL)
	    ERROR_RETURN_LOG(int, "Cannot creaete the data source page based on the imcompleted data source");


	_handle_t* handle = (_handle_t*)out;
	_module_context_t* context = (_module_context_t*)ctx;

	if(handle->dir != _DIR_OUT)
	    ERROR_RETURN_LOG(int, "Invalid type of handle, expected write, but get read");

	runtime_api_pipe_flags_t flags = itc_module_get_handle_flags(out);

	/* Since we previously make sure the size of sync buf is at most one page, so this is bounded */
	PREDICT_IMPOSSIBLE(context->async_buf_size > MODULE_TCP_MAX_ASYNC_BUF_SIZE);
	int8_t sync_buf[context->async_buf_size];

	if(flags & RUNTIME_API_PIPE_ASYNC)
	{
		LOG_DEBUG("Async write_callback called");

		if(ERROR_CODE(int) == _ensure_async_loop_init(context))
		    ERROR_RETURN_LOG(int, "Cannot initialize the async loop");

		int eos_rc = ERROR_CODE(int);
		size_t sync_data_size = 0; /* How many bytes in the sync_data buffer */
		const int8_t* sync_data = NULL; /* The pointer for the start address of buffer that haven't been written */

		/* Actually we want to write the bytes synchronizely until the scoket is not able to accept more */
		for(;handle->async_handle == NULL && eos_rc != 1;)
		{
			/* If we get here second time, it means we do not create the async handle last time, which means
			 * the _ensure_async_handle function has exhausted the sync data, that is why we can set the sync
			 * data size to 0 here anyway */
			sync_data_size = 0;
			sync_data = NULL;

			int data_source_wait = 0;

			if(context->sync_write_attempt)
			{
				LOG_DEBUG("The sync write attempt option is enabled, so try the sync write before we start async write process");
				size_t sync_buf_size = context->async_buf_size;
				for(sync_data = sync_buf; sync_buf_size > 0;)
				{
					if(ERROR_CODE(int) == (eos_rc = data_source.eos(data_source.data_handle)))
					    ERROR_RETURN_LOG(int, "data_source.eos returns a failure");

					if(eos_rc) break;

					size_t bytes_read = data_source.read(data_source.data_handle, sync_buf + sync_data_size, sync_buf_size, NULL);

					if(bytes_read == 0)
					{
						data_source_wait = 1;
						break;
					}

					if(ERROR_CODE(size_t) == bytes_read)
					    ERROR_RETURN_LOG(int, "Cannot read the data source");
					sync_data_size += bytes_read;
					sync_buf_size  -= bytes_read;
				}
			}

			/* In this case, we want to do sync write and avoid the async handle to be created if the network connection
			 * is fast enough, so if the sync data buffer is exhausted by this call, it's reasonable to try another time
			 * and do not start the async handle this time, because we want to avoid the overhead of async write itself.
			 * Once the socket is not able to accept more data, it's the best time to switch over. So if we allows the
			 * sync write attempt, then we may want to try it as many time as possible.
			 * So the force create option only needs to be turned on when the sync write attempt is off
			 **/
			size_t written = _ensure_async_handle(context, handle, sync_data, sync_data_size, data_source_wait || !context->sync_write_attempt);

			if(ERROR_CODE(size_t) == written)
			    ERROR_RETURN_LOG(int, "Cannot create async handle for the pipe");

			if(NULL != sync_data)
			    sync_data += written;
		}

		if(NULL != sync_data && sync_buf + sync_data_size > sync_data)
		{
			size_t bytes_to_write = sync_data_size - (size_t)(sync_data - sync_buf);
			LOG_DEBUG("The sync write attempt doesn't exhaust the sync buffer (%zu bytes left), "
			          "write the remaining bytes asynchronizely", bytes_to_write);
			while(bytes_to_write > 0)
			{
				size_t rc = _write_async_buf(context, handle, sync_data, bytes_to_write);
				if(ERROR_CODE(size_t) == rc)
				    ERROR_RETURN_LOG(int, "Cannot write data to async buffer");
				bytes_to_write -= rc;
				sync_data += rc;
			}
		}


		/* Finally, let's create a data source page for the data source ! */
		int rc = 0;

		/* Make sure the eos call has been called at this point */
		if(eos_rc == ERROR_CODE(int) && ERROR_CODE(int) == (eos_rc = (data_source.eos(data_source.data_handle))))
		    ERROR_RETURN_LOG(int, "data_source.eos returns an error status code");
		if(eos_rc)
		{
			LOG_DEBUG("The data stream has been exhausted by the sync write attempt, won't go any further");
			return data_source.close(data_source.data_handle);
		}

		LOG_DEBUG("The data source is not exhausted, write it to the async buffer");
		if((errno = pthread_mutex_lock(handle->async_handle->mutex)) != 0)
		    ERROR_RETURN_LOG(int, "cannot acquire the async object mutex for connection %"PRIu32, handle->idx);

		_async_buf_page_t* page = _async_buf_data_source_page_new(data_source);
		if(NULL == page) ERROR_LOG_GOTO(ASYNC_ERR, "Cannot allocate data source page for the data source");

		if(handle->async_handle->page_end != NULL) handle->async_handle->page_end->next = page;
		handle->async_handle->page_end = page;
		if(NULL == handle->async_handle->page_begin) handle->async_handle->page_begin = page;

		goto ASYNC_RET;
ASYNC_ERR:
		/* In this case we actually toke the onwership */
		rc = ERROR_CODE_OT(int);
		data_source.close(data_source.data_handle);
ASYNC_RET:
		if((errno = pthread_mutex_unlock(handle->async_handle->mutex)) != 0)
		{
			LOG_ERROR("cannot release the async object mutex for connection %"PRIu32, handle->idx);
			rc = ERROR_CODE_OT(int);
		}
		if(rc != ERROR_CODE(int) && rc != ERROR_CODE_OT(int) && module_tcp_async_write_data_ready(context->async_loop, handle->idx) == ERROR_CODE(int))
		    ERROR_RETURN_LOG(int, "cannot notify the async IO loop");
		return rc;
	}
	else
	{
		LOG_DEBUG("Sync write_callback called");
		for(;;)
		{
			int eos_rc = data_source.eos(data_source.data_handle);
			if(ERROR_CODE(int) == eos_rc)
			    ERROR_RETURN_LOG(int, "Cannot call the data_source.eos");

			if(eos_rc) break;

			/* TODO: use async buf size doesn't make sense at this point */
			size_t nbytes = data_source.read(data_source.data_handle, sync_buf, context->async_buf_size, NULL);
			if(ERROR_CODE(size_t) == nbytes)
			    ERROR_RETURN_LOG(int, "Cannot read the data_source");

			const int8_t* bytes = sync_buf;
			for(;nbytes > 0;)
			{
				ssize_t rc = write(handle->fd, bytes, nbytes);
				if(rc == 0) ERROR_RETURN_LOG(int, "Unexpected number of bytes writen, treat as an socket error");
				else if(rc > 0)
				{
					nbytes -= (size_t)rc;
					bytes += rc;
				}
				else if(errno == EAGAIN || errno == EWOULDBLOCK)
				    continue;
				else ERROR_RETURN_LOG(int, "Scoket error");
			}
		}
		return data_source.close(data_source.data_handle);
	}
}

static size_t _write(void* __restrict ctx, const void* __restrict data, size_t nbytes, void* __restrict out)
{
	if(NULL == ctx || NULL == data || NULL == out) ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const int8_t* bytes = (const int8_t*)data;

	_handle_t* handle = (_handle_t*)out;
	_module_context_t* context = (_module_context_t*)ctx;

	if(handle->dir != _DIR_OUT)
	    ERROR_RETURN_LOG(size_t, "Invalid type of handle, expected write, but get read");

	runtime_api_pipe_flags_t flags = itc_module_get_handle_flags(out);

	if(flags & RUNTIME_API_PIPE_ASYNC)
	{
		LOG_DEBUG("Async write called");

		if(ERROR_CODE(int) == _ensure_async_loop_init(context))
		    ERROR_RETURN_LOG(size_t, "Cannot initialize the async loop");

		size_t sync_write_bytes = _ensure_async_handle(context, handle, bytes, nbytes, 0);

		if(ERROR_CODE(size_t) == sync_write_bytes)
		    ERROR_RETURN_LOG(size_t, "Cannot create the async write handle");

		nbytes -= sync_write_bytes;
		bytes += sync_write_bytes;
		if(0 == nbytes) return sync_write_bytes;

		size_t async_write_bytes = _write_async_buf(context, handle, bytes, nbytes);
		if(ERROR_CODE(size_t) == async_write_bytes)
		    ERROR_RETURN_LOG(size_t, "Async write failure");

		return async_write_bytes + sync_write_bytes;
	}
	else
	{
		size_t bytes_written = 0;
		for(;nbytes > 0;)
		{
			ssize_t rc = write(handle->fd, bytes, nbytes);
			if(rc == 0) ERROR_RETURN_LOG(size_t, "Unexpected number of bytes writen, treat as an socket error");
			else if(rc > 0)
			{
				bytes_written += (size_t)rc,
				nbytes -= (size_t)rc;
				bytes += rc;
			}
			else if(errno == EAGAIN || errno == EWOULDBLOCK)
			    continue;
			else ERROR_RETURN_LOG(size_t, "Scoket error");
		}
		return bytes_written;
	}
}

static int _dealloc(void* __restrict ctx, void* __restrict pipe, int error, int purge)
{
	if(NULL == ctx || NULL == pipe) ERROR_RETURN_LOG(int, "Invalid arguments");
	_module_context_t* context = (_module_context_t*)ctx;
	_handle_t* handle = (_handle_t*)pipe;

	if(purge)
	{
		LOG_DEBUG("Both side of the pipe has been deallocated, close the socket");
		runtime_api_pipe_flags_t flags = itc_module_get_handle_flags(pipe);

		if((flags & RUNTIME_API_PIPE_PERSIST) && !error)
		{
			int mode;
			if(handle->state->unread_bytes > 0)
			{
				LOG_DEBUG("there's some more bytes to read, mark the connection to ready to read state");
				mode = MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_READ;
			}
			else
			{
				LOG_DEBUG("There's no more data in the buffer, mark the connection as wait for data state");
				mode = MODULE_TCP_POOL_RELEASE_MODE_WAIT_FOR_DATA;
			}

			/* If the user state is currently pending to push, we do not need to dispose them */
			if(handle->state->user_space_data != NULL && handle->state->user_state_pending == 0)
			{
				if(_dispose_user_state(handle->state) == ERROR_CODE(int))
				    LOG_WARNING("could not deallocate user space state, memory leak is possbile");

				handle->state->user_space_data = NULL;
				handle->state->disp = NULL;
			}

			if(handle->async_handle == NULL)
			{
				LOG_DEBUG("There's no undergoing async write op, release the connection directly");
				if(module_tcp_pool_connection_release(context->conn_pool, handle->idx, handle->state, mode) == ERROR_CODE(int))
				    ERROR_RETURN_LOG(int, "cannot release the connection");
			}
			else
			{
				LOG_DEBUG("There's an undergoing async write op, send the data_end queue message to the async IO loop");
				handle->async_handle->release_mode = mode;
				handle->async_handle->release_data = handle->state;
				if(module_tcp_async_write_data_ends(context->async_loop, handle->idx) == ERROR_CODE(int))
				    ERROR_RETURN_LOG(int, "cannot notify the async IO loop");
			}
		}
		else
		{
			if(handle->state != NULL)
			{
				_dispose_state(handle->state);
			}

			if(handle->async_handle == NULL)
			{
				LOG_DEBUG("there's no undergoing async write op, release the connection directly");
				if(module_tcp_pool_connection_release(context->conn_pool, handle->idx, NULL, MODULE_TCP_POOL_RELEASE_MODE_PURGE) == ERROR_CODE(int))
				    ERROR_RETURN_LOG(int, "cannot purge the connection");
			}
			else
			{
				LOG_DEBUG("There's an undergoing async write op, send the data end queue message to the async IO loop");
				handle->async_handle->release_mode = MODULE_TCP_POOL_RELEASE_MODE_PURGE;
				handle->async_handle->release_data = NULL;
				if(module_tcp_async_write_data_ends(context->async_loop, handle->idx) == ERROR_CODE(int))
				    ERROR_RETURN_LOG(int, "cannot notify the async IO loop");
			}
		}
	}

	return 0;
}

static int _has_unread(void* __restrict ctx, void* __restrict pipe)
{
	(void) ctx;
	_handle_t* h = (_handle_t*)pipe;

	if(h->dir == _DIR_OUT)
	{
		LOG_ERROR("Cannot perform has_unread call on an output pipe");
		return ERROR_CODE(int);
	}

	return h->has_more;
}

static int _eom(void* __restrict ctx, void* __restrict pipe, const char* buffer, size_t offset)
{
	(void) ctx;
	(void) buffer;
	_handle_t* handle = (_handle_t*)pipe;

	if(handle->last_read_offset + offset > handle->state->total_bytes) ERROR_RETURN_LOG(int, "Invalid arguments");

	handle->state->unread_bytes += (handle->state->total_bytes - handle->state->unread_bytes) - (handle->last_read_offset + offset);

	return 0;
}

static int _push_state(void* __restrict ctx, void* __restrict pipe, void* __restrict state, itc_module_state_dispose_func_t func)
{
	(void) ctx;
	_handle_t *handle = (_handle_t*) pipe;

	if(handle->state->user_space_data != state && _dispose_user_state(handle->state) == ERROR_CODE(int))
	    LOG_WARNING("Could not deallocate the previous user state, memory leak possible");

	handle->state->user_state_pending = 1;

	handle->state->user_space_data = state;

	handle->state->disp = func;

	return 0;
}

static void* _pop_state(void* __restrict ctx, void* __restrict pipe)
{
	(void)ctx;
	_handle_t *handle = (_handle_t*) pipe;

	void* ret = handle->state->user_space_data;

	return ret;
}

static void _event_loop_killed(void* __restrict ctx)
{
	_module_context_t* context = (_module_context_t*)ctx;
	module_tcp_pool_loop_killed(context->conn_pool);
}

static inline itc_module_property_value_t _make_num(int64_t n)
{
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_INT,
		.num  = n
	};
	return ret;
}

static itc_module_property_value_t _get_prop(void* __restrict ctx, const char* sym)
{
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_NONE
	};
	_module_context_t* context = (_module_context_t*)ctx;


	/* Also, any forked event loop do not have permission to access any of the config */
	if(context->fork_id != 0)
	    return ret;

	if(strcmp(sym, "port") == 0) return _make_num(context->pool_conf.port);
	else if(strcmp(sym, "ttl") == 0) return _make_num(context->pool_conf.ttl);
	else if(strcmp(sym, "size") == 0) return _make_num(context->pool_conf.size);
	else if(strcmp(sym, "event_size") == 0) return _make_num((long long)context->pool_conf.event_size);
	else if(strcmp(sym, "event_timeout") == 0) return _make_num(context->pool_conf.min_timeout);
	else if(strcmp(sym, "backlog") == 0) return _make_num(context->pool_conf.tcp_backlog);
	else if(strcmp(sym, "ipv6") == 0) return _make_num(context->pool_conf.ipv6);
	else if(strcmp(sym, "reuseaddr") == 0) return _make_num((long long)context->pool_conf.reuseaddr);
	else if(strcmp(sym, "async_buf_size") == 0) return _make_num((long long)context->async_buf_size);
	else if(strcmp(sym, "accept_retry_interval") == 0) return _make_num((long long)context->pool_conf.accept_retry_interval);
	else if(strcmp(sym, "bindaddr") == 0) //*(const char**)data = context->pool_conf.bind_addr;
	{
		size_t len;
		if(NULL == (ret.str = (char*)malloc(len = 1 + strlen(context->pool_conf.bind_addr))))
		{
			ret.type = ITC_MODULE_PROPERTY_TYPE_ERROR;
			return ret;
		}

		memcpy(ret.str, context->pool_conf.bind_addr, len);

		ret.type = ITC_MODULE_PROPERTY_TYPE_STRING;

		return ret;
	}
	else if(strcmp(sym, "nforks") == 0)
	    return _make_num((long long)module_tcp_pool_num_forks(context->conn_pool));

	return ret;
}

static int _set_prop(void* __restrict ctx, const char* sym, itc_module_property_value_t value)
{
	_module_context_t* context = (_module_context_t*)ctx;

	/* TODO: this is weird, because it sounds like different module actually shares the same configuration */
	static char bindaddr_buffer[128];
	/* For a forked module, we don't allow any property change */
	if(context->fork_id != 0) return 0;
	if(value.type == ITC_MODULE_PROPERTY_TYPE_INT)
	{
		/*if(strcmp(sym, "port") == 0) context->pool_conf.port = (uint16_t)value.num;
		else */if(strcmp(sym, "ttl") == 0) context->pool_conf.ttl = (time_t)value.num;
		else if(strcmp(sym, "size") == 0) context->pool_conf.size = (uint32_t)value.num;
		else if(strcmp(sym, "event_size") == 0) context->pool_conf.event_size = (uint32_t)value.num;
		else if(strcmp(sym, "event_timeout") == 0) context->pool_conf.min_timeout = (time_t)value.num;
		else if(strcmp(sym, "backlog") == 0) context->pool_conf.tcp_backlog = (int)value.num;
		else if(strcmp(sym, "ipv6") == 0) context->pool_conf.ipv6 = (int)value.num;
		else if(strcmp(sym, "reuseaddr") == 0) context->pool_conf.reuseaddr = (int)value.num;
		else if(strcmp(sym, "accept_retry_interval") == 0) context->pool_conf.accept_retry_interval = (uint32_t)value.num;
		else if(strcmp(sym, "async_buf_size") == 0)
		{
			context->async_buf_size = (uint32_t)value.num;
			if(context->async_buf_size > (uint32_t)getpagesize())
			{
				LOG_WARNING("Async buffer size is larger than one page, adjust it to fit one page");
				context->async_buf_size = (uint32_t)getpagesize();
			}

			if(context->async_buf_size > MODULE_TCP_MAX_ASYNC_BUF_SIZE)
			{
				LOG_WARNING("Async buffer size is larger than the hard limit, adjust it to the hard limit");
				context->async_buf_size = MODULE_TCP_MAX_ASYNC_BUF_SIZE;
			}
		}
		else return 0;
	}
	else if(value.type == ITC_MODULE_PROPERTY_TYPE_STRING)
	{
		if(strcmp(sym, "bindaddr") == 0)
		{
			const char* input = value.str;
			size_t sz = strlen(input);
			if(sz > sizeof(bindaddr_buffer)) sz = sizeof(bindaddr_buffer);
			memcpy(bindaddr_buffer, input, sz);
			bindaddr_buffer[sz] = 0;
			context->pool_conf.bind_addr = bindaddr_buffer;
		}
		else return 0;
	}
	else return 0;
	return 1;
}

void* module_tcp_module_get_pool(void* ctx)
{
	_module_context_t* context = (_module_context_t*)ctx;
	return context->conn_pool;
}

int module_tcp_module_set_port(void* ctx, uint16_t port)
{
	_module_context_t* context = (_module_context_t*)ctx;
	context->pool_conf.port = port;
	return 0;
}

static const char* _get_path(void* __restrict ctx, char* buf, size_t sz)
{
	_module_context_t* context = (_module_context_t*)ctx;
	if(context->fork_id == 0)
	    snprintf(buf, sz, "port_%u", context->pool_conf.port);
	else
	    snprintf(buf, sz, "port_%u$%d", context->pool_conf.port, context->fork_id);
	return buf;
}

static itc_module_flags_t _get_flags(void* __restrict ctx)
{
	_module_context_t* context = (_module_context_t*)ctx;
	if(context->slave_mode) return 0;
	return ITC_MODULE_FLAGS_EVENT_LOOP;
}

itc_module_t module_tcp_module_def = {
	.mod_prefix = "pipe.tcp",
	.handle_size = sizeof(_handle_t),
	.context_size = sizeof(_module_context_t),
	.module_init = _init,
	.module_cleanup = _cleanup,
	.accept = _accept,
	.deallocate = _dealloc,
	.read = _read,
	.write = _write,
	.write_callback = _write_callback,
	.has_unread_data = _has_unread,
	.eom = _eom,
	.push_state = _push_state,
	.pop_state = _pop_state,
	.event_thread_killed = _event_loop_killed,
	.get_property = _get_prop,
	.set_property = _set_prop,
	.get_path = _get_path,
	.get_flags = _get_flags,
	.get_internal_buf = _get_internal_buf,
	.release_internal_buf = _release_internal_buf
};

