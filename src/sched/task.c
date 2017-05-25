/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <predict.h>
#include <error.h>
#include <plumber.h>

#include <utils/log.h>
#include <utils/mempool/objpool.h>
#include <utils/string.h>

/**
 * @brief the task table entry
 **/
typedef struct _task_entry_t{
	sched_task_t          task;                  /*!< the task details */
	uint32_t              num_required_inputs;   /*!< how many inputs this task required */
	uint32_t              num_cancelled_inputs;  /*!< how many inputs has already been cancelled so far */
	uint32_t              num_awaiting_inputs;   /*!< how many inputs that is still in awaiting state, which means either unassigned or not ready */
	struct _task_entry_t* prev;                  /*!< the previous item in the list */
	struct _task_entry_t* next;                  /*!< the previous item in the list */
} _task_entry_t;
STATIC_ASSERTION_FIRST(_task_entry_t, task);

typedef struct _request_entry_t {
	sched_task_request_t request_id; /*!< the request id for this request */
	uint32_t num_pending_tasks;      /*!< the number of pending tasks has been created for this request */
	sched_rscope_t* scope;           /*!< the request local scope */
	struct _request_entry_t* next;   /*!< the next pointer for the request hash table */
} _request_entry_t;

/**
 * @brief The context used by a task table
 **/
struct _sched_task_context_t {
	_task_entry_t**       task_table;     /*!< The hash table used to organize tasks */
	_request_entry_t**    request_table;  /*!< The requet information table, maps the request id to the request entry */
	_task_entry_t*        queue_head;     /*!< The ready queue head */
	_task_entry_t*        queue_tail;     /*!< The ready queue tail */
	uint32_t              queue_size;     /*!< The size of the queue */
};

/** @brief the memory pool used for the task entry */
static mempool_objpool_t* _task_pool = NULL;
/** @brief the memory pool used for the request entrt */
static mempool_objpool_t* _request_pool = NULL;

/**
 * @brief enqueue a task to the ready quee
 * @param task the task to insert
 * @return nothing
 **/
static inline void _enqueue(sched_task_context_t* ctx, _task_entry_t* task)
{
	if(NULL != ctx->queue_tail) ctx->queue_tail->next = task;
	else ctx->queue_head = task;
	ctx->queue_tail = task;
	ctx->queue_size ++;
}

/**
 * @brief remove the first task from the ready queue
 * @note this will remove and return the first task in the queue
 * @return the first task in the queue, if the queue is empty, return NULL
 **/
static inline _task_entry_t* _dequeue(sched_task_context_t* ctx)
{
	if(ctx->queue_size == 0) return NULL;
	_task_entry_t* ret = ctx->queue_head;
	if(NULL == (ctx->queue_head = ctx->queue_head->next))
	    ctx->queue_tail = NULL;
	ctx->queue_size --;
	return ret;
}

/**
 * @brief create a new request entry object for the given request id
 * @param request the given request id
 * @return the newly created request, NULL on error case
 **/
static inline _request_entry_t* _request_entry_new(sched_task_request_t request)
{
	LOG_DEBUG("New request entry has been created");
	_request_entry_t* ret = (_request_entry_t*)mempool_objpool_alloc(_request_pool, 0);
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new request");
	if(NULL == (ret->scope = sched_rscope_new()))
	{
		ERROR_PTR_RETURN_LOG("Cannot create scope object for the new request");
		mempool_objpool_dealloc(_request_pool, ret);
		return NULL;
	}
	ret->num_pending_tasks = 0;
	ret->request_id = request;
	ret->next = NULL;

	return ret;
}

/**
 * @brief dispose a used request entry object
 * @param entry the request entry object
 * @return status code
 **/
static inline int _request_entry_free(_request_entry_t* entry)
{
	int rc = 0;
	if(NULL != entry->scope && sched_rscope_free(entry->scope) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == mempool_objpool_dealloc(_request_pool, entry))
	    rc = ERROR_CODE(int);

	return rc;
}

/**
 * @brief find the request entry object for the given request id
 * @param request the request id we want to look for
 * @return the request entry object, or NULL if not found
 **/
static inline _request_entry_t* _request_entry_find(const sched_task_context_t* ctx, sched_task_request_t request)
{
	uint32_t slot = (uint32_t)(request % SCHED_TASK_TABLE_SLOT_SIZE);
	_request_entry_t* ret;
	for(ret = ctx->request_table[slot]; NULL != ret && request != ret->request_id; ret = ret->next);
	return ret;
}
/**
 * @brief inset a new request entry with the given request id to the request table
 * @note this do not guarantee the uniqueness of the request id in the table
 * @param request the request id
 * @return the newly created entry or NULL on error case
 **/
static inline _request_entry_t* _request_entry_insert(sched_task_context_t* ctx, sched_task_request_t request)
{
	uint32_t slot = (uint32_t)(request % SCHED_TASK_TABLE_SLOT_SIZE);
	_request_entry_t* ret = _request_entry_new(request);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Canont create new request node for the request");

	ret->next = ctx->request_table[slot];
	return ctx->request_table[slot] = ret;
}

/**
 * @brief delete the request entry object from the request table if the entry exists
 * @param request the request id
 * @return the number of entry has been deleted or error code
 **/
static inline int _request_entry_delete(sched_task_context_t* ctx, sched_task_request_t request)
{
	uint32_t slot = (uint32_t)(request % SCHED_TASK_TABLE_SLOT_SIZE);
	_request_entry_t* prev = NULL;

	/* if the request we are looking for is not the first */
	if(ctx->request_table[slot] == NULL || ctx->request_table[slot]->request_id != request)
	{
		for(prev = ctx->request_table[slot];
		    prev != NULL && prev->next != NULL && prev->next->request_id != request;
		    prev = prev->next);
		if(prev == NULL || prev->next == NULL) return 0;
	}

	_request_entry_t* to_delete = NULL == prev ? ctx->request_table[slot] : prev->next;
	if(prev == NULL) ctx->request_table[slot] = to_delete->next;
	else prev->next = to_delete->next;

	if(ERROR_CODE(int) == _request_entry_free(to_delete))
	    ERROR_RETURN_LOG(int, "Cannot dispose the request entry");

	return 1;
}

static inline uint32_t _hash(const sched_service_t* service, sched_task_request_t request_id, sched_service_node_id_t node_id)
{
	uintptr_t service_id = (uintptr_t)service;
	uint64_t hash = service_id +
	                (service_id << 32) +
	                (request_id << 16) +
	                node_id;
	return (uint32_t)(hash % SCHED_TASK_TABLE_SLOT_SIZE);
}

/**
 * @brief this function is used to make sure that the runtime task is instantiated
 * @note the purpose of this function is allowing lazy instantiation of a runtime task. If the
 *       task is cancelled, the task won't gets instantiated
 * @param task the scheduler task to instantiate
 * @return the status code
 **/
static inline int _task_guareentee_instantiated(_task_entry_t* task)
{
	if(task->task.exec_task != NULL) return 0;

	task->task.exec_task = sched_service_create_task(task->task.service, task->task.node);
	if(NULL == task->task.exec_task) ERROR_RETURN_LOG(int, "Cannot create exec task for the node");
	return 0;
}

static inline _task_entry_t* _task_entry_new(sched_task_context_t* ctx, const sched_service_t* service, sched_task_request_t request, sched_service_node_id_t node)
{
	/* The reason why we initialize the pool here is beacuse we may want to start multiple thread later,
	 * But init function won't get called when a thread gets spawn. So it init here. */
	_request_entry_t* req = _request_entry_find(ctx, request);
	if(NULL == req) ERROR_PTR_RETURN_LOG("Cannot get the request entry object");

	_task_entry_t* ret = mempool_objpool_alloc(_task_pool, 0);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new task entry");
	ret->task.ctx = ctx;
	ret->task.exec_task = NULL;
	ret->prev = ret->next = NULL;

	ret->task.scope = req->scope;

	if(NULL == sched_service_get_incoming_pipes(service, node, &ret->num_required_inputs))
	    ERROR_LOG_GOTO(ERR, "Cannot get the incoming pipe list");

	ret->num_awaiting_inputs = ret->num_required_inputs;
	ret->num_cancelled_inputs = 0;

	ret->task.service = service;
	ret->task.node    = node;
	ret->task.request = request;
	/* This function do not actually instanitate a runtime task, because it may be cancelled
	 * The runtime task will be created by _task_instantiate function and should be called
	 * if there's a pipe which is not cancelled */
	ret->task.exec_task = NULL;
	req->num_pending_tasks ++;

	return ret;
ERR:

	if(NULL != ret) mempool_objpool_dealloc(_task_pool, ret);
	return NULL;
}

static inline _task_entry_t* _task_table_find(const sched_task_context_t* ctx, const sched_service_t* service, sched_task_request_t request, sched_service_node_id_t node)
{
	uint32_t h = _hash(service, request, node);

	_task_entry_t* ret;
	for(ret = ctx->task_table[h];
	    NULL != ret && PREDICT_FALSE(ret->task.service != service || ret->task.request != request || ret->task.node != node);
	    ret = ret->next);
	return ret;
}

static inline _task_entry_t* _task_table_insert(sched_task_context_t* ctx, const sched_service_t* service, sched_task_request_t request, sched_service_node_id_t node)
{
	_task_entry_t* ret = _task_entry_new(ctx, service, request, node);

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create new task for service");

	uint32_t h = _hash(service, request, node);

	ret->next = ctx->task_table[h];
	if(NULL != ctx->task_table[h]) ctx->task_table[h]->prev = ret;
	ctx->task_table[h] = ret;

	return ret;
}


static inline void _task_table_delete(sched_task_context_t* ctx, _task_entry_t* task)
{
	uint32_t h = _hash(task->task.service, task->task.request, task->task.node);

	if(task->prev != NULL) task->prev->next = task->next;
	else ctx->task_table[h] = task->next;
	if(task->next != NULL) task->next->prev = task->prev;

	task->prev = task->next = NULL;
}

int sched_task_init()
{
	if(NULL == (_task_pool = mempool_objpool_new(sizeof(_task_entry_t))))
	    ERROR_RETURN_LOG(int, "Cannot create new object pool for the task entry");
	if(NULL == (_request_pool = mempool_objpool_new(sizeof(_request_entry_t))))
	    ERROR_RETURN_LOG(int, "Cannot create new object pool for the request entry");
	return 0;
}

sched_task_context_t* sched_task_context_new()
{
	sched_task_context_t* ret = (sched_task_context_t*)calloc(1, sizeof(*ret));

	if(NULL == (ret->task_table = (_task_entry_t**)calloc(SCHED_TASK_TABLE_SLOT_SIZE, sizeof(ret->task_table[0]))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the task hash table");

	if(NULL == (ret->request_table = (_request_entry_t**)calloc(SCHED_TASK_TABLE_SLOT_SIZE, sizeof(ret->request_table[0]))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the request hash table");
	return ret;

ERR:
	if(NULL != ret->task_table) free(ret->task_table);
	if(NULL != ret->request_table) free(ret->request_table);
	free(ret);
	return NULL;
}

int sched_task_context_free(sched_task_context_t* ctx)
{
	if(NULL == ctx)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int i = 0;
	int rc = 0;

	/* dispose the task table first */
	if(ctx->task_table != NULL)
	{
		for(i = 0; i < SCHED_TASK_TABLE_SLOT_SIZE; i ++)
		{
			_task_entry_t* ptr;
			for(ptr = ctx->task_table[i]; NULL != ptr;)
			{
				_task_entry_t* cur = ptr;
				ptr = ptr->next;
				if(cur->task.exec_task != NULL && runtime_task_free(cur->task.exec_task) == ERROR_CODE(int))
				{
					LOG_WARNING("Cannot dispose the servlet task from the table");
					rc = ERROR_CODE(int);
				}

				if(mempool_objpool_dealloc(_task_pool, cur) == ERROR_CODE(int))
				{
					LOG_WARNING("Cannot dispose the scheduler task from the table");
					rc = ERROR_CODE(int);
				}
			}
		}

		_task_entry_t *ptr;
		for(ptr = ctx->queue_head; ptr;)
		{
			_task_entry_t* cur = ptr;
			ptr = ptr->next;
			if(cur->task.exec_task != NULL && runtime_task_free(cur->task.exec_task) == ERROR_CODE(int))
			{
				LOG_WARNING("Cannot dispose the servlet task from the qeueue");
				rc = ERROR_CODE(int);
			}

			if(mempool_objpool_dealloc(_task_pool, cur) == ERROR_CODE(int))
			{
				LOG_WARNING("Cannot dispose the schuler task from the ready list");
				rc = ERROR_CODE(int);
			}
		}
		free(ctx->task_table);
	}

	/* then dispose the request table */
	if(NULL != ctx->request_table)
	{
		for(i = 0; i < SCHED_TASK_TABLE_SLOT_SIZE; i ++)
		{
			_request_entry_t* req;
			for(req = ctx->request_table[i]; NULL != req;)
			{
				_request_entry_t* cur = req;
				req = req->next;
				if(ERROR_CODE(int) == _request_entry_free(cur))
				{
					LOG_WARNING("Cannot dispose the memory used by the request entry in the request table");
					rc = ERROR_CODE(int);
				}
			}
		}

		free(ctx->request_table);
	}

	free(ctx);

	return rc;
}

int sched_task_finalize()
{
	int rc = 0;

	if(_task_pool != NULL && mempool_objpool_free(_task_pool) == ERROR_CODE(int))
	{
		LOG_WARNING("Cannot dispose the object memory pool for task table");
		rc = ERROR_CODE(int);
	}

	if(_request_pool != NULL && ERROR_CODE(int) == mempool_objpool_free(_request_pool))
	{
		LOG_WARNING("Cannot dispose the object memory pool for the request table");
		rc = ERROR_CODE(int);
	}

	return rc;
}
/**
 * @brief add a pipe to the task's pipe handle list
 * @param task the task to which we want to add the pipe
 * @param pipe the target pipe id
 * @param handle the pipe handle
 * @param claim indicates if we needs claim the onwership of the pipe
 * @return status code
 **/
static inline int _task_add_pipe(_task_entry_t* task, runtime_api_pipe_id_t pipe, itc_module_pipe_t* handle, int claim)
{
	if(NULL == task || NULL == handle || pipe == ERROR_CODE(runtime_api_pipe_id_t)) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(_task_guareentee_instantiated(task) == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot instanitate the runtime task");

	if((size_t)pipe >= task->task.exec_task->npipes) ERROR_RETURN_LOG(int, "Invalid pipe ID");

	if(NULL != task->task.exec_task->pipes[pipe]) ERROR_RETURN_LOG(int, "The task pipe slot #%d is previously assigned", pipe);

	task->task.exec_task->pipes[pipe] = handle;

	if(claim)
	{
		/* claim the onwership of this pipe */
		itc_module_pipe_ownership_t* ownership = (itc_module_pipe_ownership_t*)handle;
		ownership->owner = task;
	}

	return 0;
}

int sched_task_pipe_ready(sched_task_t* task)
{
	_task_entry_t* task_internal = (_task_entry_t*)task;
	if(NULL == task_internal)
	{
		LOG_NOTICE("Unassigned pipe");
		return 0;
	}

	if(0 == --task_internal->num_awaiting_inputs)
	{
		LOG_TRACE("this task <RequestId=%"PRIu64", NodeId=%"PRIu32"> is ready to run, "
		          "remove it from the task table and add it to ready queue",
		          task->request, task->node);
		_task_table_delete(task->ctx, task_internal);
		_enqueue(task->ctx, task_internal);
	}

	return 0;
}

sched_task_request_t sched_task_new_request(sched_task_context_t* ctx, const sched_service_t* service, itc_module_pipe_t* input_pipe, itc_module_pipe_t* output_pipe)
{
	static __thread sched_task_request_t ret = 0;
	const sched_service_pipe_descriptor_t* pipe_model;
	_task_entry_t *in_task, *out_task;
	_request_entry_t* req_ent;

	if(NULL == service || NULL == input_pipe || NULL == output_pipe) ERROR_RETURN_LOG(sched_task_request_t, "Invalid arguments");

	if(NULL == (req_ent = (_request_entry_insert(ctx, ret))))
	    ERROR_RETURN_LOG(sched_task_request_t, "Cannot create new request entry object");

	pipe_model = sched_service_to_pipe_desc(service);

	in_task = _task_table_insert(ctx, service, ret, pipe_model->source_node_id);

	if(NULL == in_task) ERROR_LOG_GOTO(ERR, "Cannot create input task for the incoming request");

	/* If the input node and output node are actually the same one, we do not need do insertion any more */
	out_task = (pipe_model->source_node_id == pipe_model->destination_node_id) ?
	           in_task:
	           _task_table_insert(ctx, service, ret, pipe_model->destination_node_id);

	if(NULL == out_task) ERROR_LOG_GOTO(ERR, "Cannot create output task for the incoming request");

	if(in_task->num_awaiting_inputs != 0) ERROR_LOG_GOTO(ERR, "Invalid task: Input pipe should not have pending inputs");

	if(_task_guareentee_instantiated(in_task) == ERROR_CODE(int) || _task_guareentee_instantiated(out_task) == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "Cannot instantiate task");

	in_task->task.exec_task->pipes[pipe_model->source_pipe_desc] = input_pipe;
	out_task->task.exec_task->pipes[pipe_model->destination_pipe_desc] = output_pipe;

	/* claim the onwership of the request pipes */
	((itc_module_pipe_ownership_t*)input_pipe)->owner = in_task;
	((itc_module_pipe_ownership_t*)output_pipe)->owner = out_task;

	_task_table_delete(ctx, in_task);
	_enqueue(ctx, in_task);

	LOG_INFO("Request object #%"PRIu64" has been created", ret);
	return ret ++;
ERR:
	_request_entry_delete(ctx, ret);
	return ERROR_CODE(sched_task_request_t);
}

int sched_task_input_pipe(sched_task_context_t* ctx, const sched_service_t* service, sched_task_request_t request,
                          sched_service_node_id_t node, runtime_api_pipe_id_t pipe,
                          itc_module_pipe_t* handle)
{
	_task_entry_t* task = _task_table_find(ctx, service, request, node);
	if(NULL == task) task = _task_table_insert(ctx, service, request, node);

	if(ERROR_CODE(int) == _task_add_pipe(task, pipe, handle, 1))
	    ERROR_RETURN_LOG(int, "Cannot add pipe to the task");

	/* Because we have a signle thread model for each request, it we can simply that the pipe is ready once is gets assigned */
	if(sched_task_pipe_ready((sched_task_t*)task) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot set the shadow pipe to ready state");

	int rc;
	if(ERROR_CODE(int) == (rc = itc_module_is_pipe_cancelled(handle)))
	    ERROR_RETURN_LOG(int, "Cannot check if the pipe is already cancelled");

	if(rc != 0 && ERROR_CODE(int) == sched_task_input_cancelled((sched_task_t*)task))
	    ERROR_RETURN_LOG(int, "Cannot notify the cancel state to its owner");

	return 0;
}

int sched_task_output_pipe(sched_task_t* task, runtime_api_pipe_id_t pipe, itc_module_pipe_t* handle)
{
	if(NULL == task || pipe == ERROR_CODE(runtime_api_pipe_id_t) || NULL == handle)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_task_entry_t* task_internal = (_task_entry_t*)task;

	return _task_add_pipe(task_internal, pipe, handle, 1);
}

int sched_task_output_shadow(sched_task_t* task, runtime_api_pipe_id_t pipe, itc_module_pipe_t* handle)
{
	if(NULL == task || pipe == ERROR_CODE(runtime_api_pipe_id_t) || NULL == handle)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc_shadow = itc_module_is_pipe_shadow(handle);
	if(ERROR_CODE(int) == rc_shadow) ERROR_RETURN_LOG(int, "Cannot check if the pipe is shadow");
	if(!rc_shadow) ERROR_RETURN_LOG(int, "Invalid pipe handle: try to add a normal pipe handle as a shadow");

	int rc_input = itc_module_is_pipe_input(handle);
	if(ERROR_CODE(int) == rc_input) ERROR_RETURN_LOG(int, "Cannot check if the pipe is input pipe");
	if(!rc_input) ERROR_RETURN_LOG(int, "Invalid pipe handle: try to add a non-input pipe handle as a shadow");

	_task_entry_t* task_internal = (_task_entry_t*)task;

	return _task_add_pipe(task_internal, pipe, handle, 0);
}

int sched_task_input_cancelled(sched_task_t* task)
{
	if(NULL == task) ERROR_RETURN_LOG(int, "Invalid arguments");

	_task_entry_t* task_internal = (_task_entry_t*)task;

	task_internal->num_cancelled_inputs ++;

	return 0;
}

__attribute__((used)) static inline const char* _get_task_args(const _task_entry_t* task, char* buffer, size_t size)
{
	uint32_t argc;
	char const* const* argv;

	if(NULL == (argv = sched_service_get_node_args(task->task.service, task->task.node, &argc)))
	    return NULL;

	string_buffer_t sb;
	string_buffer_open(buffer, size, &sb);
	int first = 1;

	for(;argc; argc --, argv ++)
	    if(first) string_buffer_append(argv[0], &sb), first = 0;
	    else string_buffer_appendf(&sb, " %s", argv[0]);

	string_buffer_close(&sb);

	return buffer;
}
/**
 * @brief mark a pipe as cancelled
 * @param service the service graph
 * @param request the request id
 * @param node the target node id
 * @param pid the target pipe id
 * @return the status code
 **/
static inline int _pipe_cancel(sched_task_context_t* ctx, const sched_service_t* service, sched_task_request_t request, sched_service_node_id_t node, runtime_api_pipe_id_t pid)
{
	_task_entry_t* receiver = _task_table_find(ctx, service, request, node);
	if(NULL == receiver) receiver = _task_table_insert(ctx, service, request, node);

	if(NULL == receiver) ERROR_RETURN_LOG(int, "Cannot get the receiver task");

	receiver->num_cancelled_inputs ++;

	/* If this is a unassigned pipe, basically we needs to mark it as ready, otherwise, it already marked as ready when assign pipe handle to it */
	if(receiver->task.exec_task == NULL || receiver->task.exec_task->pipes[pid] == NULL)
	    return sched_task_pipe_ready(&receiver->task);
	else
	    return 0;
}

sched_task_t* sched_task_next_ready_task(sched_task_context_t* ctx)
{
	for(;;)
	{
		_task_entry_t* next = _dequeue(ctx);
		if(NULL == next) return NULL;

		/* Check if this task is cancelled */
		if(next->num_required_inputs > 0 && next->num_cancelled_inputs == next->num_required_inputs)
		{
#ifdef LOG_DEBUG_ENABLED
			char arg_buffer[1024];
			_get_task_args(next, arg_buffer, sizeof(arg_buffer));
			LOG_DEBUG("Task `%s' is cancelled because the all its inputs are marked as cancelled", arg_buffer);
#endif

			const sched_cnode_info_t* cnodes = sched_service_get_cnode_info(next->task.service);
			if(NULL == cnodes) ERROR_PTR_RETURN_LOG("Cannot get the critical node info of the service");

			if(cnodes->boundary[next->task.node] == NULL)
			{
				uint32_t i, size;
				const sched_service_pipe_descriptor_t* result = sched_service_get_outgoing_pipes(next->task.service, next->task.node, &size);

				if(NULL == result) ERROR_PTR_RETURN_LOG("Cannot get outgoing pipe for task");

				for(i = 0; i < size; i ++)
				    if(ERROR_CODE(int) == _pipe_cancel(ctx, next->task.service, next->task.request, result[i].destination_node_id, result[i].destination_pipe_desc))
				        ERROR_PTR_RETURN_LOG("Cannot cancel the downstream pipe");
			}
			else
			{
				LOG_TRACE("Critical task has been cancelled, cancel all the task in the cluster");
				const sched_cnode_boundary_t* boundary = cnodes->boundary[next->task.node];

				uint32_t i;
				for(i = 0; i < boundary->count; i ++)
				    if(ERROR_CODE(int) == _pipe_cancel(ctx, next->task.service, next->task.request, boundary->dest[i].node_id, boundary->dest[i].pipe_desc))
				        ERROR_PTR_RETURN_LOG("Cannot cancel the cluster boundary pipe");

				if(boundary->output_cancelled)
				{
					LOG_TRACE("Output task is in the critical cluster, cancel it");
					sched_service_node_id_t output = sched_service_get_output_node(next->task.service);
					if(ERROR_CODE(sched_service_node_id_t) == output) ERROR_PTR_RETURN_LOG("Cannot get the output node id");

					_task_entry_t* out_task = _task_table_find(ctx, next->task.service, next->task.request, output);
					if(NULL == out_task) ERROR_PTR_RETURN_LOG("Cannot cancel the output task");

					_task_table_delete(ctx, out_task);

					if(ERROR_CODE(int) == sched_task_free(&out_task->task)) ERROR_PTR_RETURN_LOG("Cannot dispose the output task");
				}

			}

			if(ERROR_CODE(int) == sched_task_free(&next->task))
			    ERROR_PTR_RETURN_LOG("Cannot dispose the cancelled task");
		}
		else
		{
#ifdef LOG_TRACE_ENABLED
			char arg_buffer[1024];
			_get_task_args(next, arg_buffer, sizeof(arg_buffer));
			LOG_TRACE("task `%s' has been picked up as next step", arg_buffer);
#endif
			return &next->task;
		}
	}
}

int sched_task_free(sched_task_t* task)
{
	int rc = 0;
	sched_task_context_t* ctx = task->ctx;
	_request_entry_t* req = _request_entry_find(ctx, task->request);
	if(NULL == req) rc = ERROR_CODE(int);

	if(NULL != task->exec_task)
	    rc = runtime_task_free(task->exec_task);

	if(mempool_objpool_dealloc(_task_pool, task) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(NULL != req && 0 == --req->num_pending_tasks)
	{
		LOG_DEBUG("Request %"PRIu64" is done", req->request_id);
		if(ERROR_CODE(int) == _request_entry_delete(ctx, req->request_id))
		    rc = ERROR_CODE(int);
	}

	return rc;
}

int sched_task_request_status(const sched_task_context_t* ctx, sched_task_request_t request)
{
	if(ERROR_CODE(sched_task_request_t) == request)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return NULL != _request_entry_find(ctx, request);
}

