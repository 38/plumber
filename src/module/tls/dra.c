/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include <error.h>
#include <predict.h>
#include <barrier.h>

#include <utils/log.h>
#include <utils/mempool/objpool.h>
#include <utils/mempool/page.h>
#include <utils/static_assertion.h>

#include <itc/module_types.h>
#include <itc/module.h>

#include <module/tls/api.h>
#include <module/tls/bio.h>
#include <module/tls/dra.h>

/**
 * @brief the common header of a DRA object, which represent a signle DRA data source used by the
 *        transporation layer module
 **/
typedef struct {
	SSL*                         ssl;           /*!< The SSL context we should use */
	module_tls_bio_context_t*    bio_ctx;       /*!< The BIO context for the current pipe we operate */
	uint32_t*                    dra_counter;   /*!< The variable that indicates how many DRA is going on */
	enum {
		_DATA_BUF,                              /*!< This DRA object is the wrapper for data buffer */
		_DATA_SRC                               /*!< This DRA object is the wrapper for data source callback */
	}                            type;          /*!< The type of this DRA */
	int8_t*                      buffer_begin;  /*!< The address where the unread data buffer begins */
	int8_t*                      buffer_end;    /*!< The address where the unread data buffer ends */
	int8_t*                      buffer_page;   /*!< The page we used as buffer, for the callback DRA, this is the read buffer, for the buffer DRA this is the data buffer */
	union {
		itc_module_data_source_t callback;      /*!< The actual callback data source, only valid for the data source mode */
		size_t                   data_size;     /*!< The actual daat section size, only valid for the data buffer mode */
	};
} _dra_t;

/**
 * @brief the pool used to allocate the DRA objects
 **/
static mempool_objpool_t* _dra_pool;

/**
 * @brief the small buffer pool is the pool which we used for the data buffer which is smaller than a page
 **/
static mempool_objpool_t** _small_buffer_pool;

/**
 * @brief the number of bytes in a single page
 **/
static size_t _page_size;

int module_tls_dra_init()
{
	if(NULL == (_dra_pool = mempool_objpool_new(sizeof(_dra_t))))
	    ERROR_RETURN_LOG(int, "Cannot allocate memory pool for the DRA callback object");

	_page_size = (size_t)getpagesize();

	size_t pool_size = 32;
	unsigned pool_count = 0, i;
	for(;pool_size < _page_size; pool_size *= 2, pool_count ++);

	if(NULL == (_small_buffer_pool = (mempool_objpool_t**)calloc(pool_count, sizeof(mempool_objpool_t*))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the small buffer pools");

	for(pool_size = 32, i = 0; pool_size < _page_size; pool_size *= 2, i ++)
	    if(NULL == (_small_buffer_pool[i] = mempool_objpool_new((uint32_t)pool_size)))
	        ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the small buffer pool with size %zu", pool_size);

	return 0;

ERR:
	if(NULL != _small_buffer_pool)
	{
		for(i = 0; i < pool_count; i ++)
		    if(NULL != _small_buffer_pool[i] && ERROR_CODE(int) == mempool_objpool_free(_small_buffer_pool[i]))
		        LOG_WARNING("Cannot dispose the small buffer pool");

		free(_small_buffer_pool);
	}

	if(NULL != _dra_pool)
	{
		mempool_objpool_free(_dra_pool);
		_dra_pool = NULL;
	}

	return ERROR_CODE(int);
}

int module_tls_dra_finalize()
{
	int rc = 0;
	if(NULL != _dra_pool && ERROR_CODE(int) == mempool_objpool_free(_dra_pool))
	{
		LOG_ERROR("Cannot dispose the memory pool for the DRA callback objects");
		rc = ERROR_CODE(int);
	}

	if(NULL != _small_buffer_pool)
	{
		size_t pool_size;
		unsigned i = 0;
		for(pool_size = 32; pool_size < _page_size; pool_size *= 2, i ++)
		    if(NULL != _small_buffer_pool[i] && ERROR_CODE(int) == mempool_objpool_free(_small_buffer_pool[i]))
		    {
			    LOG_ERROR("Cannot dispose the small buffer pool for size %zu", pool_size);
			    rc = ERROR_CODE(int);
		    }

		free(_small_buffer_pool);
	}

	return rc;
}

/**
 * @brief get the small object pool in proper size
 * @param size the size of data we are handling
 * @return the pool we should use, NULL if there's no such pool
 **/
static inline mempool_objpool_t* _get_small_object_pool(size_t size)
{
	if(size > _page_size / 2)
	{
		LOG_DEBUG("The buffer size is larger than half of the size of a page, use the entire page for the buffer");
		return NULL;
	}

	size_t pool_size;
	unsigned pool = 0;
	for(pool_size = 32; pool_size < size; pool_size *= 2, pool ++);

	LOG_DEBUG("The buffer size is smaller than small buffer pool in size %zu, use the pool", pool_size);

	return _small_buffer_pool[pool];
}

/**
 * @brief create a new buffer DRA
 * @param draparam The DRA parameter
 * @param buffer the buffer we want to create the DRA object for
 * @param size the size of the buffer
 * @note this function will take at most _page_size bytes of data and data beyond that size will not be taken care of
 *       The caller need to check the result->data_size to see if the buffer is exhausted
 * @return the DRA object we created, NULL on error
 **/
static inline _dra_t* _buffer_dra_new(module_tls_dra_param_t draparam, const void* buffer, size_t size)
{
	_dra_t* ret = (_dra_t*)mempool_objpool_alloc(_dra_pool);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate new DRA object");

	ret->ssl = draparam.ssl;
	ret->bio_ctx = draparam.bio;
	ret->dra_counter = draparam.dra_counter;

	mempool_objpool_t* small_pool = _get_small_object_pool(size);

	if(small_pool == NULL)
	{
		if(NULL == (ret->buffer_page = (int8_t*)mempool_page_alloc()))
		    ERROR_LOG_GOTO(ERR, "Cannot allocate the buffer page");
		ret->data_size = size;
		if(ret->data_size > _page_size)
		    ret->data_size = _page_size;
	}
	else
	{
		if(NULL == (ret->buffer_page = (int8_t*)mempool_objpool_alloc(small_pool)))
		    ERROR_LOG_GOTO(ERR, "Cannot allocate the buffer page from small buffer pool");

		ret->data_size = size;
	}

	memcpy(ret->buffer_page, buffer, ret->data_size);
	ret->buffer_begin = ret->buffer_page;
	ret->buffer_end   = ret->buffer_page + ret->data_size;
	ret->type = _DATA_BUF;

	return ret;

ERR:
	mempool_objpool_dealloc(_dra_pool, ret);
	return NULL;
}

/**
 * @brief Create the DRA object wrap for a data source callback
 * @param draparam the DRA parameters
 * @param data_source the data source we would use
 * @return the DRA we created or NULL on error cases
 **/
static inline _dra_t* _callback_dra_new(module_tls_dra_param_t draparam, itc_module_data_source_t data_source)
{
	_dra_t* ret = (_dra_t*)mempool_objpool_alloc(_dra_pool);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate new DRA object");

	ret->ssl = draparam.ssl;
	ret->bio_ctx = draparam.bio;
	ret->dra_counter = draparam.dra_counter;
	if(NULL == (ret->buffer_page = (int8_t*)mempool_page_alloc()))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate the buffer page");

	ret->callback = data_source;
	ret->buffer_begin = ret->buffer_end = NULL;
	ret->type = _DATA_SRC;

	return ret;

ERR:
	mempool_objpool_dealloc(_dra_pool, ret);
	return NULL;
}

/**
 * @brief Dispose a used DRA callback wrapper
 * @param dra the DRA callback wrapper to dispose
 * @return status code
 **/
static inline int _dra_free(_dra_t* dra)
{
	int rc = 0;
	mempool_objpool_t* pool = NULL;
	switch(dra->type)
	{
		case _DATA_SRC:
		    if(dra->callback.close != NULL && ERROR_CODE(int) == dra->callback.close(dra->callback.data_handle))
		        rc = ERROR_CODE(int);
		    break;
		case _DATA_BUF:
		    pool = _get_small_object_pool(dra->data_size);
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid DRA type");
	}

	if(NULL != dra->buffer_page)
	{
		if(NULL == pool && ERROR_CODE(int) == mempool_page_dealloc(dra->buffer_page))
		    rc = ERROR_CODE(int);

		if(NULL != pool && ERROR_CODE(int) == mempool_objpool_dealloc(pool, dra->buffer_page))
		    rc = ERROR_CODE(int);
	}

	if(ERROR_CODE(int) == mempool_objpool_dealloc(_dra_pool, dra))
	    rc = ERROR_CODE(int);

	return rc;
}


/****** Callback functions ***********/

/**
 * @brief write bytes to the SSL context
 * @param ssl the ssl context
 * @param buffer the data buffer
 * @param size the size of the buffer
 * @return the number of bytes has been written or error code
 **/
static inline size_t _write_ssl(SSL* ssl, const void* buffer, size_t size)
{
	int rc = SSL_write(ssl, buffer, (int)size);

	if(rc <= 0)
	{

		int reason = SSL_get_error(ssl, rc);
		if(reason == SSL_ERROR_WANT_WRITE)
		{
			LOG_DEBUG("The transporation layer buffer is full");
			return 0;
		}
		ERROR_RETURN_LOG(size_t, "SSL_write returns an error: %s", ERR_error_string(ERR_get_error(), NULL));
	}

	LOG_DEBUG("%d raw bytes has been written to the transporation layer buffer", rc);

	return (size_t)rc;
}

/**
 * @brief the callback function which is used to load data from DRA
 * @param handle the DRA callback object
 * @param buffer the buffer to return the result
 * @param count how many bytes we are requiring
 * @return the number of bytes that is actually read
 **/
static inline size_t _dra_read(void* __restrict handle, void* __restrict buffer, size_t count)
{
	_dra_t* dra = (_dra_t*)handle;

	size_t ret = 0;

	dra->bio_ctx->buffer = (char*)buffer;
	dra->bio_ctx->bufsize = count;

	if(dra->buffer_begin - dra->buffer_end == 0)
	{
		if(dra->type == _DATA_BUF)
		    LOG_DEBUG("Current data buffer DRA is exhausted, stopping");
		else if(dra->type == _DATA_SRC)
		{
			LOG_DEBUG("The DRA data source buffer doesn't contains any data, try to load data");

			int eos_rc;
			if(ERROR_CODE(int) == (eos_rc = dra->callback.eos(dra->callback.data_handle)))
			    ERROR_LOG_GOTO(ERR, "EOS callback of RLS byte stream returns an error");

			if(eos_rc)
			    LOG_DEBUG("Current data source DRA is exhausted, stopping");
			else
			{
				size_t rc = dra->callback.read(dra->callback.data_handle, dra->buffer_page, _page_size);
				if(ERROR_CODE(size_t) == rc)
				    ERROR_LOG_GOTO(ERR, "Cannot read data from the RLS byte stream");

				dra->buffer_begin = dra->buffer_page;
				dra->buffer_end = dra->buffer_page + rc;
			}
		}
	}

	if(dra->buffer_end - dra->buffer_begin > 0)
	{
		LOG_DEBUG("The DRA callback read buffer have unprocessed data, go ahead process those data");

		size_t before = dra->bio_ctx->bufsize;
		size_t raw_bytes_written = _write_ssl(dra->ssl, dra->buffer_begin, (size_t)(dra->buffer_end - dra->buffer_begin));
		size_t after = dra->bio_ctx->bufsize;

		if(ERROR_CODE(size_t) == raw_bytes_written)
		    ERROR_LOG_GOTO(ERR, "Cannot write raw bytes to SSL context");

		ret += before - after;
		dra->buffer_begin += raw_bytes_written;

		if(raw_bytes_written == 0)
		    LOG_DEBUG("The transporation layer buffer is full, exiting");
	}

	goto RET;
ERR:
	ret = ERROR_CODE(size_t);
RET:
	dra->bio_ctx->buffer = NULL;
	return ret;
}

/**
 * @brief the callback function used to check if the DRA callback object is currently empty
 * @param handle the callback handle
 * @note this is safe, because this function will only called from the thread whoever writes the transporation layer. <br/>
 *       And this is the same thread which is the only one consuming the byte stream. So when we are here, the active state
 *       can not be changed anymore, because the thread other than that can append to the DRA queue only, which won't change
 *       the DRA active state.
 * @return the result or error code
 **/
static inline int _dra_eos(const void* __restrict handle)
{
	const _dra_t* dra = (const _dra_t*)handle;

	int ret = 1;
	switch(dra->type)
	{
		case _DATA_SRC:
		    if(ERROR_CODE(int) == (ret = dra->callback.eos(dra->callback.data_handle)))
		        ERROR_RETURN_LOG(int, "The inner callback's eos function returns an error");
		case _DATA_BUF:
		    ret = ret && (dra->buffer_begin == dra->buffer_end);
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid TLS DRA type");
	}

	return ret;
}

/**
 * @brief the callback function used when the transporation layer want to close the DRA
 * @param handle the handle to close
 * @return status code
 **/
static inline int _dra_close(void* __restrict handle)
{
	_dra_t* dra = (_dra_t*)handle;

	int rc = 0;
	uint32_t new_val;

	if(ERROR_CODE(int) == _dra_free(dra))
	    rc = ERROR_CODE(int);

	/* We need a CAS derement here, because this function may be called from both IO loop and worker thread */
	do {
		new_val = *dra->dra_counter - 1;
	} while(!__sync_bool_compare_and_swap(dra->dra_counter, new_val + 1, new_val));

	return rc;
}

/**
 * @brief Start a new DRA process for given DRA callback object
 * @param dra The DRA callback object to start
 * @return status code, ownership transfer is possible, by which means the data
 *         source has been disposed by this function
 **/
static inline int _start_dra(_dra_t* dra)
{
	itc_module_data_source_t data_source = {
		.data_handle = dra,
		.read        = _dra_read,
		.eos         = _dra_eos,
		.close       = _dra_close
	};

	uint32_t new_val;

	/* We need CAS, because this may be modified by the IO loop */
	do {
		new_val = *dra->dra_counter + 1;
	} while(!__sync_bool_compare_and_swap(dra->dra_counter, new_val - 1, new_val));

	switch(itc_module_pipe_write_data_source(data_source, NULL, dra->bio_ctx->pipe))
	{
		case ERROR_CODE(int):
		    /* The inner data source may be closed by the read function, but it's disposed anyway after this line of code */
		    _dra_close(dra);

		case ERROR_CODE_OT(int):
		    /* If the inner data source is killed by the transporation layer module, do not kill it twice! */
		    LOG_ERROR( "Cannot write the callback function to the transporation layer");

		    /* Adjust the proper return value */
		    if(dra->type == _DATA_SRC)
		        return ERROR_CODE_OT(int);
		    else
		        return  ERROR_CODE(int);
		case 0:
		    /* Because it's success state, so we are good, the caller will assume the callback has been taken care of by this function */
		    if(ERROR_CODE(int) == _dra_close(dra))
		        ERROR_RETURN_LOG(int, "Cannot close the TLS DRA data stream callback");
		    break;
		case  1:
		    /* The owership has been taken by the transporation layer, we are free */
		    LOG_DEBUG("The DRA data source callback has been accepted by the transporation layer module");
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Code bug: Invalid return value from itc_module_pipe_write_data_source");
	}

	return 0;
}

int module_tls_dra_write_callback(module_tls_dra_param_t draparam, itc_module_data_source_t source)
{
	if(NULL == draparam.ssl || NULL == draparam.bio || NULL == draparam.dra_counter ||
	   source.read == NULL || source.eos == NULL || source.close == NULL)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_dra_t* dra = _callback_dra_new(draparam, source);
	if(NULL == dra)
	    ERROR_RETURN_LOG(int, "Cannot create new DRA callback wrapper");

	return _start_dra(dra);
}


size_t module_tls_dra_write_buffer(module_tls_dra_param_t draparam, const char* data, size_t size)
{
	 if(NULL == data || NULL == draparam.ssl || NULL == draparam.bio || NULL == draparam.dra_counter)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");


	if(0 == *draparam.dra_counter)
	{
		LOG_DEBUG("There's no undergoing DRA, rejecting all the write request");
		return 0;
	}


	_dra_t* dra = _buffer_dra_new(draparam, data, size);
	if(NULL == dra)
	    ERROR_RETURN_LOG(size_t, "Cannot create the DRA buffer wrapper");

	size_t ret = dra->data_size;

	int rc = _start_dra(dra);

	if(rc == ERROR_CODE(int) || rc == ERROR_CODE_OT(int))
	    return ERROR_CODE(size_t);

	return ret;
}
