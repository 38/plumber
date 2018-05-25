/**
 * Copyright (C) 2017-2018, Hao Hou
 * Copyright (C) 2018, Feng Liu
 **/
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include <error.h>
#include <utils/log.h>
#include <utils/mempool/objpool.h>

#include <runtime/api.h>
#include <sched/rscope.h>

#define _NULL_ENTRY ERROR_CODE(runtime_api_scope_token_t)

/**
 * @brief the actual data type for the request local scope
 **/
struct _sched_rscope_t {
	uint64_t                  id;       /*!< the identifier for current scope */
	runtime_api_scope_token_t head;     /*!< the head of the linked list */
};

/**
 * @brief represent a scope entity data
 **/
typedef struct {
	runtime_api_scope_entity_t    entity;   /*!< the scope entity */
	uint32_t                      refcnt;   /*!< the reference counter, this is needed because when the token is taken by the async loop, we want it alive until the stream dead */
} _scope_entity_t;

/**
 * @brief represent a single scope entry
 **/
typedef struct {
	runtime_api_scope_token_t     next;      /*!< the next entry in the scope linked list */
	uint64_t                      scope_id;  /*!< the scope id for the owner scope */
	_scope_entity_t*              data;      /*!< the actual pointer definition */
} _entry_t;


/**
 * @brief the actual data structure for a RSL stream
 * @note  the reason why we dont use token is because when we add the token to async write loop,
 *        the async loop can not access the entity table, because it's a thread local
 **/
struct _sched_rscope_stream_t {
	_scope_entity_t*          entity;   /*!< the underlying scope entity for this stream */
	runtime_api_scope_token_t token;    /*!< the token id for this entity */
	void*                     handle;   /*!< the handle of the stream */
};

/**
 * @brief this is the actual thread local data storage for each scheduler loop
 * @note We distinuish the concept of cached and unused. When the entry is assigned to one request, it will be in use
 *       however, once the entry is deallocated, the entry will be added to the cached list and will be in cached state
 *       rather than unused.
 *       The only case we have unused entry is after memory is allocated and the entry is either in use or cached. <br/>
 **/
static __thread struct {
	uint32_t                  capacity;   /*!< how many items in the table */
	runtime_api_scope_token_t cached;     /*!< the head of cached unused token list */
	runtime_api_scope_token_t unused;     /*!< the range [unused, capacity) is the unused list */
	_entry_t*                 data;       /*!< the actual entry table */
} _entry_table;

/**
 * @brief the memory pool that is used to allocate the request local scope object
 **/
static mempool_objpool_t* _rscope_pool;

/**
 * @brief the memory pool that is used to allocate the entity data object
 **/
static mempool_objpool_t* _entity_pool;

/**
 * @brief the memory pool that is used to allocate the byte stream handle object
 **/
static mempool_objpool_t* _stream_pool;

int sched_rscope_init()
{
	if(NULL == (_rscope_pool = mempool_objpool_new(sizeof(sched_rscope_t))))
	    ERROR_RETURN_LOG(int, "Cannot allocate object pool for request local scope objects");

	if(NULL == (_stream_pool = mempool_objpool_new(sizeof(sched_rscope_stream_t))))
	    ERROR_RETURN_LOG(int, "Cannot allocate object pool for byte stream handle objects");

	if(NULL == (_entity_pool = mempool_objpool_new(sizeof(_scope_entity_t))))
	    ERROR_RETURN_LOG(int, "Cannot allocate scope entity object pool");

	return 0;
}

int sched_rscope_finalize()
{
	int rc = 0;
	if(NULL != _rscope_pool && ERROR_CODE(int) == mempool_objpool_free(_rscope_pool))
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot dispose the object pool for request local scope objects");
	}

	if(NULL != _stream_pool && ERROR_CODE(int) == mempool_objpool_free(_stream_pool))
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot dispose the object pool for the stream handle objects");
	}

	if(NULL != _entity_pool && ERROR_CODE(int) == mempool_objpool_free(_entity_pool))
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot dispose the object pool for scope entities");
	}

	return rc;
}

/**
 * @brief try to deallocate the scope entity
 * @param entity the entity to deallocate
 * @return status code
 **/
static inline int _dispose_scope_entity(_scope_entity_t* entity)
{
	uint32_t new_refcnt;
	do {
		new_refcnt = entity->refcnt - 1;
	} while(!__sync_bool_compare_and_swap(&entity->refcnt, new_refcnt + 1, new_refcnt));

	if(new_refcnt == 0)
	{
		int rc = 0;
		if(NULL != entity->entity.data && NULL != entity->entity.free_func &&
		   ERROR_CODE(int) == entity->entity.free_func(entity->entity.data))
		{
			LOG_ERROR("The entity free callback returns an error code");
			rc = ERROR_CODE(int);
		}
		if(ERROR_CODE(int) == mempool_objpool_dealloc(_entity_pool, entity))
		{
			LOG_ERROR("Cannot dispose the entity object");
			rc = ERROR_CODE(int);
		}

		return rc;
	}

	return 0;
}

int sched_rscope_init_thread()
{
	_entry_table.capacity = SCHED_RSCOPE_ENTRY_TABLE_INIT_SIZE;
	_entry_table.cached   = _NULL_ENTRY;
	_entry_table.unused   = 0;
	_entry_table.data     = (_entry_t*)malloc(sizeof(_entry_table.data[0]) * _entry_table.capacity);

	if(NULL == _entry_table.data)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the scope entry table");

	return 0;
}

int sched_rscope_finalize_thread()
{
	int rc = 0;

	if(NULL != _entry_table.data)
	{
		runtime_api_scope_token_t i;
		for(i = 0; i < _entry_table.unused; i ++)
		    if(_entry_table.data[i].data != NULL && ERROR_CODE(int) == _dispose_scope_entity(_entry_table.data[i].data))
		        rc = ERROR_CODE(int);
		free(_entry_table.data);
	}

	return rc;
}

/**
 * @brief allocate a new entry object from the entry table
 * @return the entry table that has been allocated, or error code
 **/
static inline runtime_api_scope_token_t _entry_alloc(void)
{
	runtime_api_scope_token_t ret = _NULL_ENTRY;

	/* First, we need to check if there's an cached entry */
	if(_entry_table.cached != _NULL_ENTRY)
	{
		ret = _entry_table.cached;
		_entry_table.cached = _entry_table.data[ret].next;

		if(NULL == (_entry_table.data[ret].data = mempool_objpool_alloc(_entity_pool)))
		    ERROR_RETURN_LOG_ERRNO(runtime_api_scope_token_t, "Cannot allocate memory for the entity data pool");
		else
		    memset(_entry_table.data[ret].data, 0, sizeof(*_entry_table.data[ret].data));

		return ret;
	}

	/* Then we must have at least one unused entry, otherwise we fail */
	if(_entry_table.unused >= _entry_table.capacity)
	{
		if(_entry_table.capacity * 2 > SCHED_RSCOPE_ENTRY_TABLE_SIZE_LIMIT)
		    ERROR_RETURN_LOG(runtime_api_scope_token_t,
		                     "The entry table size reach the limit "
		                     "(SCHED_RSCOPE_ENTRY_TABLE_SIZE_LIMIT), which is %u",
		                     SCHED_RSCOPE_ENTRY_TABLE_SIZE_LIMIT);
		LOG_DEBUG("Request local scope entry table needs to be resized to %u", _entry_table.capacity * 2);
		_entry_t* new_table = (_entry_t*)realloc(_entry_table.data, sizeof(_entry_table.data[0]) * _entry_table.capacity * 2);
		if(NULL == new_table)
		    ERROR_RETURN_LOG_ERRNO(runtime_api_scope_token_t, "Cannot resize the entry table");
		_entry_table.data = new_table;
		_entry_table.capacity *= 2;
	}

	ret = _entry_table.unused;
	if(NULL == (_entry_table.data[ret].data = mempool_objpool_alloc(_entity_pool)))
	    ERROR_RETURN_LOG_ERRNO(runtime_api_scope_token_t, "Cannot allocate memory for the entity data pool");
	else
	    memset(_entry_table.data[ret].data, 0, sizeof(*_entry_table.data[ret].data));

	_entry_table.data[ret].next = _NULL_ENTRY;
	_entry_table.unused ++;

	return ret;
}

sched_rscope_t* sched_rscope_new()
{
	static __thread uint64_t next_scope_id = 0;

	sched_rscope_t* ret = mempool_objpool_alloc(_rscope_pool);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the request local scope");

	ret->head = _NULL_ENTRY;
	ret->id   = next_scope_id ++;

	LOG_DEBUG("Request local scope %"PRIu64" has been created", ret->id);

	return ret;
}

int sched_rscope_free(sched_rscope_t* scope)
{
	int rc = 0;

	if(NULL == scope) ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_api_scope_token_t tok;
	for(tok = scope->head; tok != _NULL_ENTRY;)
	{
		runtime_api_scope_token_t cur_tok = tok;
		_entry_t* entry = _entry_table.data + tok;
		tok = _entry_table.data[tok].next;
		if(ERROR_CODE(int) == _dispose_scope_entity(entry->data))
		    rc = ERROR_CODE(int);

		entry->next = _entry_table.cached;
		entry->data = NULL;
		_entry_table.cached = cur_tok;
	}
#ifdef LOG_ERROR_ENABLED
	uint64_t scope_id = scope->id;
#endif

	if(ERROR_CODE(int) == mempool_objpool_dealloc(_rscope_pool, scope))
	    rc = ERROR_CODE(int);

	if(rc != ERROR_CODE(int))
	    LOG_DEBUG("Request local scope %"PRIu64" has been disposed", scope_id);
	else
	    LOG_ERROR("Request local scope %"PRIu64" has been disposed with error code", scope_id);

	return rc;
}

runtime_api_scope_token_t sched_rscope_add(sched_rscope_t* scope, const runtime_api_scope_entity_t* pointer)
{
	if(NULL == scope || NULL == pointer || NULL == pointer->data || NULL == pointer->free_func)
	    ERROR_RETURN_LOG(runtime_api_scope_token_t, "Invalid arguments");

	runtime_api_scope_token_t ret = _entry_alloc();
	if(_NULL_ENTRY == ret)
	    ERROR_RETURN_LOG(runtime_api_scope_token_t, "Cannot allocate new entry for the pointer");

	LOG_DEBUG("The pointer has new entry token %u", ret);

	_entry_t* entry = _entry_table.data + ret;
	entry->data->entity = *pointer;
	entry->data->refcnt = 1;
	entry->next = scope->head;
	entry->scope_id = scope->id;
	scope->head = ret;

	LOG_DEBUG("Request local scope entry %u has been used for request local scope %"PRIu64, ret, scope->id);

	return ret;
}

int sched_rscope_copy(sched_rscope_t* scope, runtime_api_scope_token_t token, sched_rscope_copy_result_t* result)
{
	if(NULL == scope || _NULL_ENTRY == token || token >= _entry_table.capacity || NULL == result)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _entry_t* source = _entry_table.data + token;

	if(NULL == source->data->entity.copy_func)
	    ERROR_RETURN_LOG(int, "This entry doesn't support copy");

	runtime_api_scope_entity_t target = source->data->entity;
	target.data = NULL;

	if(NULL == (target.data = source->data->entity.copy_func(source->data->entity.data)))
	    ERROR_RETURN_LOG(int, "Cannot copy the data");

	if(_NULL_ENTRY == (result->token = sched_rscope_add(scope, &target)))
	    ERROR_LOG_GOTO(ERR, "Cannot add the copied token to the scope");

	result->ptr = target.data;

	LOG_DEBUG("Request local scope entry %u has been duplicated to entry %u", token, result->token);

	return 0;
ERR:
	if(NULL != target.data && NULL != target.free_func)
	    target.free_func(target.data);
	return ERROR_CODE(int);
}

const void* sched_rscope_get(const sched_rscope_t* scope, runtime_api_scope_token_t token)
{
	if(NULL == scope || _NULL_ENTRY == token || token >= _entry_table.capacity)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(_entry_table.data[token].data->entity.data == NULL ||
	   _entry_table.data[token].scope_id != scope->id)
	    ERROR_PTR_RETURN_LOG("Invalid token id, %u does not belong to scope %"PRIu64, token, scope->id);

	return _entry_table.data[token].data->entity.data;
}

sched_rscope_stream_t* sched_rscope_stream_open(runtime_api_scope_token_t token)
{
	if(_NULL_ENTRY == token || token >= _entry_table.capacity || _entry_table.data[token].data == NULL)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const _entry_t* target = _entry_table.data + token;

	if(target->data->entity.open_func == NULL || target->data->entity.close_func == NULL ||
	   target->data->entity.read_func == NULL || target->data->entity.eos_func == NULL)
	    ERROR_PTR_RETURN_LOG("The byte stream interface is not fully supported by the RLS entity %u",
	    token);

	sched_rscope_stream_t* ret = mempool_objpool_alloc(_stream_pool);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the stream handle object for RLS token %u" , token);

	ret->entity = target->data;
	ret->token = token;
	if(NULL == (ret->handle = target->data->entity.open_func(target->data->entity.data)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the RLS token as a byte stream, RLS token: %u", token);

	LOG_DEBUG("RLS token %u has been successfully opened as a byte stream", token);

	uint32_t old_refcnt;
	do {
		old_refcnt = ret->entity->refcnt;
	} while(!__sync_bool_compare_and_swap(&ret->entity->refcnt, old_refcnt, old_refcnt + 1));

	return ret;
ERR:
	if(NULL != ret) mempool_objpool_dealloc(_stream_pool, ret);
	return NULL;
}

int sched_rscope_stream_close(sched_rscope_stream_t* stream)
{
	if(NULL == stream)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;

	_scope_entity_t* target = stream->entity;

	if(NULL != stream->handle && ERROR_CODE(int) == target->entity.close_func(stream->handle))
	{
		LOG_ERROR("Cannot close the byte stream handle for token %u", stream->token);
		rc = ERROR_CODE(int);
	}

	LOG_DEBUG("Byte stream for RLS token %u has been closed", stream->token);

	if(ERROR_CODE(int) == _dispose_scope_entity(target))
	{
		LOG_ERROR("Cannot deallocate the entity object");
		rc = ERROR_CODE(int);
	}

	if(ERROR_CODE(int) == mempool_objpool_dealloc(_stream_pool, stream))
	{
		LOG_ERROR("Cannot deallocate the stream handle object");
		rc = ERROR_CODE(int);
	}

	return rc;
}

int sched_rscope_stream_eos(const sched_rscope_stream_t* stream)
{
	if(NULL == stream)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _scope_entity_t* target = stream->entity;

	return target->entity.eos_func(stream->handle);
}

size_t sched_rscope_stream_read(sched_rscope_stream_t* stream, void* buffer, size_t count)
{
	if(NULL == stream || NULL == buffer || count == ERROR_CODE(size_t))
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const _scope_entity_t* target = stream->entity;

	size_t ret = target->entity.read_func(stream->handle, buffer, count);

	if(ERROR_CODE(size_t) != ret)
	    LOG_DEBUG("%zu bytes has been read from the RSL stream %u", ret, stream->token);
	else
	    LOG_ERROR("The read callback for RLS stream %u has returned an error", stream->token);

	return ret;
}

int sched_rscope_stream_get_event(sched_rscope_stream_t* stream, runtime_api_scope_ready_event_t* buf)
{
	if(NULL == stream || NULL == buf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_scope_entity_t* ent = stream->entity;

	if(ent->entity.event_func == NULL)
	    return 0;

	return ent->entity.event_func(stream->handle, buf);
}

int sched_rscope_get_hash(runtime_api_scope_token_t token, uint64_t out[2])
{
	if(_NULL_ENTRY == token || token >= _entry_table.capacity || _entry_table.data[token].data == NULL)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _entry_t* target = _entry_table.data + token;

	if(target->data->entity.hash_func == NULL)
		return 0;

	return target->data->entity.hash_func(target->data->entity.data, out);
}
