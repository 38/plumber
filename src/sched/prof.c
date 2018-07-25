/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <inttypes.h>
#include <stdio.h>

#include <constants.h>
#include <error.h>

#include <itc/module_types.h>
#include <itc/module.h>

#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <sched/service.h>
#include <sched/prof.h>

#include <lang/prop.h>

#include <utils/log.h>
#include <utils/string.h>
#include <utils/static_assertion.h>
#include <utils/thread.h>

/**
 * @brief the time accumulator
 **/
typedef struct {
	uint64_t total_time;   /*!< the total time elapsed on this node in ns */
	uint64_t exec_count;   /*!< the number of executions during the time */
} _accu_t;

/**
 * @brief the profiler array
 **/
typedef struct {
	sched_service_node_id_t cur_node; /*!< the current node that is being measured */
	struct timespec start_time;       /*!< the timestamp when the timer started */
	uintpad_t __padding__[0];
	_accu_t  data[0];                 /*!< the actual array */
} _prof_array_t;
STATIC_ASSERTION_SIZE(_prof_array_t, data, 0);
STATIC_ASSERTION_LAST(_prof_array_t, data);

/**
 * @brief the actual data structure for a profiler
 **/
struct _sched_prof_t {
	sched_service_node_id_t serv_size;   /*!< the size of the service graph */
	thread_pset_t*          thread_data; /*!< the thread data */
};

/**
 * @brief by default we do not enable the profiler
 **/
static uint32_t _prof_enabled = 0;

/**
 * @brief the profiling data file
 **/
static FILE* _prof_output;

/**
 * @brief create a new profiler array with n slots
 * @param tid the thread id
 * @param caller the caller of the fucntion
 * @return the result array
 **/
static inline void* _prof_array_new(uint32_t tid, const void* caller)
{
	(void)tid;
	const sched_prof_t* prof = (const sched_prof_t*)caller;

	size_t size = sizeof(_prof_array_t) + sizeof(_accu_t) * prof->serv_size;

	_prof_array_t* ret = (_prof_array_t*)calloc(1, size);

	ret->cur_node = ERROR_CODE(sched_service_node_id_t);

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allcoate memory for the profiler array");

	return ret;
}

/**
 * @brief dispose a profiler array
 * @param arr the array to dispose
 * @param caller the caller to the function
 * @return status code
 **/
static inline int _prof_array_free(void* arr, const void* caller)
{
	(void)caller;

	free(arr);
	return 0;
}

int sched_prof_new(const sched_service_t* service, sched_prof_t** result)
{
	if(NULL == service || NULL == result) ERROR_RETURN_LOG(int, "Invalid arguments");

	*result = NULL;

	if(!_prof_enabled)
	{
		LOG_DEBUG("The profiler is disabled!");
		return 0;
	}


	sched_service_node_id_t serv_size = (sched_service_node_id_t)sched_service_get_num_node(service);
	if(ERROR_CODE(sched_service_node_id_t) == serv_size)
		ERROR_RETURN_LOG(int, "Cannot get the size of the service");

	sched_prof_t* ret = NULL;
	ret = (sched_prof_t*)malloc(sizeof(sched_prof_t));
	ret->thread_data = NULL;

	if(NULL == ret) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the profiler");
	ret->serv_size = serv_size;
	if(NULL == (ret->thread_data = thread_pset_new(SCHED_PROF_INIT_THREAD_CAPACITY, _prof_array_new, _prof_array_free, ret)))
		ERROR_LOG_GOTO(ERR, "Cannot create thread data array");
	*result = ret;
	return 0;
ERR:
	if(ret->thread_data != NULL) thread_pset_free(ret->thread_data);
	free(ret);
	return ERROR_CODE(int);
}

int sched_prof_free(sched_prof_t* prof)
{
	if(NULL == prof) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;
	rc = thread_pset_free(prof->thread_data);
	free(prof);

	return rc;
}

int sched_prof_start_timer(sched_prof_t* prof, sched_service_node_id_t node)
{
	if(NULL == prof || ERROR_CODE(sched_service_node_id_t) == node || node >= prof->serv_size)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_prof_array_t* acc = (_prof_array_t*)thread_pset_acquire(prof->thread_data);
	if(NULL == acc)
		ERROR_RETURN_LOG(int, "Cannot get the profiler instance for this thread");

	if(acc->cur_node != ERROR_CODE(sched_service_node_id_t))
		ERROR_RETURN_LOG(int, "Previous profiler session is not closed yet");

	acc->cur_node = node;
	if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &acc->start_time) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot get the start timestamp");

	return 0;
}

int sched_prof_stop_timer(sched_prof_t* prof)
{
	if(NULL == prof) ERROR_RETURN_LOG(int, "Invalid arguments");

	_prof_array_t* acc = (_prof_array_t*)thread_pset_acquire(prof->thread_data);
	if(NULL == acc)
		ERROR_RETURN_LOG(int, "Cannot get the profiler instance for this thread");
	if(acc->cur_node == ERROR_CODE(sched_service_node_id_t))
		ERROR_RETURN_LOG(int, "Timer is not started yet");

	_accu_t* cell = acc->data + acc->cur_node;

	acc->cur_node = ERROR_CODE(sched_service_node_id_t);

	struct timespec end_time;
	if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot get the end timestamp");


	uint64_t time = ((uint64_t)(end_time.tv_sec - acc->start_time.tv_sec) * 1000000000ull);

	if(end_time.tv_nsec > acc->start_time.tv_nsec)
		time += (uint64_t)(end_time.tv_nsec - acc->start_time.tv_nsec);
	else
		time -= (uint64_t)(acc->start_time.tv_nsec - end_time.tv_nsec);

	cell->exec_count ++;
	cell->total_time += time;

	return 0;
}

int sched_prof_flush(sched_prof_t* prof)
{
	if(NULL == prof) ERROR_RETURN_LOG(int, "Invalid arguments");

	_prof_array_t* acc = (_prof_array_t*)thread_pset_acquire(prof->thread_data);
	if(NULL == acc)
		ERROR_RETURN_LOG(int, "Cannot get the profiler instance for this thread");

	sched_service_node_id_t size = prof->serv_size, i;

	if(_prof_output == NULL)
	{
		for(i = 0; i < size; i ++)
			LOG_NOTICE("Profiler: Thread=%u\tNode=%u\tCount=%"PRIu64"\tTime=%"PRIu64">\tAverage %lf",
			            thread_get_id(), i, acc->data[i].exec_count, acc->data[i].total_time,
			            ((double)acc->data[i].total_time) / (double)acc->data[i].exec_count);
	}
	else
	{
		flockfile(_prof_output);
		for(i = 0; i < size; i ++)
		{
			sched_prof_record_t item = {
				.thread = thread_get_id(),
				.count  = acc->data[i].exec_count,
				.time   = acc->data[i].total_time,
				.node   = i
			};
			fwrite(&item, sizeof(sched_prof_record_t), 1, _prof_output);
		}
		fflush(_prof_output);
		funlockfile(_prof_output);
	}

	memset(acc->data, 0, sizeof(_accu_t) * size);

	return 0;
}

static inline int _set_prop(const char* symbol, lang_prop_value_t value, const void* data)
{
	(void) data;
	if(NULL == symbol || LANG_PROP_TYPE_ERROR == value.type || LANG_PROP_TYPE_NONE == value.type)
		ERROR_RETURN_LOG(int, "Invalid arguments");
	if(strcmp(symbol, "enabled") == 0)
	{
		if(value.type != LANG_PROP_TYPE_INTEGER) ERROR_RETURN_LOG(int, "Type mismatch");
		_prof_enabled = (uint32_t)value.num;
		if(_prof_enabled) LOG_TRACE("Profiler is enabled");
		else LOG_TRACE("Profiler is disabled");
	}
	else if(strcmp(symbol, "output") == 0)
	{
		if(value.type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mistach");
		const char* path = value.str;
		if(NULL == path) ERROR_RETURN_LOG(int, "Cannot get the string value");
		if(path[0] == 0)
		{
			if(NULL != _prof_output)
			{
				LOG_TRACE("Outputing profiler data to log");
				fclose(_prof_output);
			}
		}
		else
		{
			if(NULL != _prof_output) fclose(_prof_output);
			_prof_output = fopen(path, "wb");
			if(NULL == _prof_output) ERROR_RETURN_LOG(int, "Cannot open the output file");
		}
	}
	else
	{
		LOG_WARNING("Unrecognized symbol name %s", symbol);
		return 0;
	}

	return 1;
}

int sched_prof_init()
{
	lang_prop_callback_t cb = {
		.param = NULL,
		.get   = NULL,
		.set   = _set_prop,
		.symbol_prefix = "profiler"
	};

	if(ERROR_CODE(int) == lang_prop_register_callback(&cb))
		ERROR_RETURN_LOG(int, "Cannot register callback for the runtime prop callback");

	return 0;
}

int sched_prof_finalize()
{
	if(NULL != _prof_output) fclose(_prof_output);

	return 0;
}
