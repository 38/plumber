/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#define NTHREADS 64

typedef struct {
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	uint32_t        flag;
	pthread_cond_t stage_cond;
	pthread_mutex_t stage_mutex;
	uint32_t stage;
	uint32_t id;
	int rc;
	int state;
} thread_data_t;
pthread_t T[NTHREADS];
thread_data_t data[NTHREADS];
static inline int _wait_stage(thread_data_t* data, uint32_t flag)
{
	ASSERT_OK(pthread_mutex_lock(&data->mutex), CLEANUP_NOP);
	while(data->flag <= flag)
	    ASSERT_OK(pthread_cond_wait(&data->cond, &data->mutex), CLEANUP_NOP);
	ASSERT_OK(pthread_mutex_unlock(&data->mutex), CLEANUP_NOP);

	return 0;
}
static inline int _notify_stage(thread_data_t* data, uint32_t stage)
{
	ASSERT_OK(pthread_mutex_lock(&data->stage_mutex), CLEANUP_NOP);
	data->stage= stage;
	ASSERT_OK(pthread_cond_signal(&data->stage_cond), CLEANUP_NOP);
	ASSERT_OK(pthread_mutex_unlock(&data->stage_mutex), CLEANUP_NOP);
	return 0;
}
static inline int _next_stage(void)
{
	int i;
	for(i = 0; i < NTHREADS; i ++)
	{
		ASSERT_OK(pthread_mutex_lock(&data[i].mutex), CLEANUP_NOP);
		data[i].flag ++;
		ASSERT_OK(pthread_cond_signal(&data[i].cond), CLEANUP_NOP);
		ASSERT_OK(pthread_mutex_unlock(&data[i].mutex), CLEANUP_NOP);
	}
	return 0;
}
static inline int _sync_stage(uint32_t thread_id, uint32_t stage)
{

	ASSERT_OK(pthread_mutex_lock(&data[thread_id].stage_mutex), CLEANUP_NOP);

	while(data[thread_id].stage < stage)
	    ASSERT_OK(pthread_cond_wait(&data[thread_id].stage_cond, &data[thread_id].stage_mutex), CLEANUP_NOP);
	ASSERT_OK(pthread_mutex_unlock(&data[thread_id].stage_mutex), CLEANUP_NOP);
	return 0;
}
static inline int _sync_stage_all(uint32_t stage)
{
	uint32_t i;
	for(i = 0; i < NTHREADS; i ++)
	    ASSERT_OK(_sync_stage(i, stage), CLEANUP_NOP);
	return 0;
}
static inline int _validate_stage(uint32_t stage)
{
	_sync_stage_all(stage);
	uint32_t i;
	for(i = 0; i < NTHREADS; i ++)
	    ASSERT_OK(data[i].rc, CLEANUP_NOP);
	return 0;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////
void* thread_main(void* d)
{
	thread_data_t* data = (thread_data_t*)d;
	if(_wait_stage(data, 0) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);

	LOG_DEBUG("Test started");
	LOG_DEBUG("Test token allocation");

	itc_equeue_token_t token = itc_equeue_module_token(1, ITC_EQUEUE_EVENT_TYPE_IO);
	if(token == ERROR_CODE(itc_equeue_token_t)) data->rc = ERROR_CODE(int);
	if(ERROR_CODE(itc_equeue_token_t) != itc_equeue_scheduler_token()) data->rc = ERROR_CODE(int);
	LOG_DEBUG("Token = %u", token);
	if(_notify_stage(data, 1) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);

	if(_wait_stage(data, 1) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);
	LOG_DEBUG("test equeue_put");
	itc_equeue_event_t event = {
		.type = ITC_EQUEUE_EVENT_TYPE_IO,
		.io = {
			.in = (itc_module_pipe_t*) data,
			.out = (itc_module_pipe_t*) data
		}
	};
	if(itc_equeue_put(token, event) == ERROR_CODE(int))
	{
		LOG_ERROR("failed to put");
		data->rc = ERROR_CODE(int);
	}

	if(_notify_stage(data, 2) == ERROR_CODE(int))
	{
		LOG_ERROR("failed to notifiy stage 2");
		data->rc = ERROR_CODE(int);
	}

	if(_wait_stage(data, 2) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);
	LOG_DEBUG("Test writer wait");

	data->state = 0;
	if(itc_equeue_put(token, event) == ERROR_CODE(int))
	{
		LOG_ERROR("failed to put");
		data->rc = ERROR_CODE(int);
	}
	else data->state ++;

	if(itc_equeue_put(token, event) == ERROR_CODE(int))
	{
		LOG_ERROR("failed to put");
		data->rc = ERROR_CODE(int);
	}
	else data->state ++;

	if(_notify_stage(data, 3) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);

	if(_wait_stage(data, 3) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);
	LOG_DEBUG("full test");
	uintptr_t i;
	for(i = 0; i < 1000; i ++)
	{
		itc_equeue_event_t e = {
			.type = ITC_EQUEUE_EVENT_TYPE_IO,
			.io = {
				.in = (itc_module_pipe_t*)(data->id * 1000 + i + 1),
				.out = (itc_module_pipe_t*)(data->id * 1000 + i + 1)
			}
		};
		if(itc_equeue_put(token, e) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);
	}

	if(_notify_stage(data, 4) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);

	if(_wait_stage(data, 4) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);
	LOG_DEBUG("random sleep test");
	for(i = 0; i < 100; i ++)
	{
		itc_equeue_event_t e = {
			.type = ITC_EQUEUE_EVENT_TYPE_IO,
			.io = {
				.in = (itc_module_pipe_t*)(data->id * 100 + i + 1),
				.out = (itc_module_pipe_t*)(data->id * 100 + i + 1)
			}
		};
		usleep((useconds_t)(rand() % 100));
		if(itc_equeue_put(token, e) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);
	}

	if(_notify_stage(data, 5) == ERROR_CODE(int)) data->rc = ERROR_CODE(int);
	LOG_DEBUG("Test ended");
	return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////

itc_equeue_token_t sched_token;
int test_token(void)
{
	ASSERT_RETOK(itc_equeue_token_t, sched_token = itc_equeue_scheduler_token(), CLEANUP_NOP);
	ASSERT_OK(_next_stage(), CLEANUP_NOP);
	ASSERT_OK(_validate_stage(1), CLEANUP_NOP);
	return 0;
}

int test_put(void)
{
	ASSERT_OK(_next_stage(),  CLEANUP_NOP);
	ASSERT_OK(_validate_stage(2), CLEANUP_NOP);

	ASSERT(itc_equeue_empty(sched_token) == 0, CLEANUP_NOP);

	int i;
	itc_equeue_event_t e;
	int flag[NTHREADS] = {};
	for(i = 0; i < NTHREADS; i ++)
	{
		itc_equeue_event_mask_t mask = ITC_EQUEUE_EVENT_MASK_NONE;
		ITC_EQUEUE_EVENT_MASK_ADD(mask, ITC_EQUEUE_EVENT_TYPE_IO);
		ASSERT_OK(itc_equeue_take(sched_token, mask ,&e), CLEANUP_NOP);
		ASSERT(e.io.in == e.io.out, CLEANUP_NOP);
		long int n = ((thread_data_t*)e.io.in) - data;
		ASSERT(n >= 0 && n < NTHREADS, CLEANUP_NOP);
		ASSERT(flag[n] == 0, CLEANUP_NOP);
		flag[n] = 1;
	}
	ASSERT(itc_equeue_empty(sched_token) >= 1, CLEANUP_NOP);

	return 0;
}
int test_writer_wait(void)
{
	ASSERT_OK(_next_stage(), CLEANUP_NOP);

	int i;
	itc_equeue_event_t e;
	for(i = 0; i < 2 * NTHREADS; i ++)
	{
		itc_equeue_event_mask_t mask = ITC_EQUEUE_EVENT_MASK_NONE;
		ITC_EQUEUE_EVENT_MASK_ADD(mask, ITC_EQUEUE_EVENT_TYPE_IO);
		ASSERT_OK(itc_equeue_wait(sched_token, NULL, NULL), CLEANUP_NOP);
		ASSERT_OK(itc_equeue_take(sched_token, mask, &e), CLEANUP_NOP);
		ASSERT(e.io.in == e.io.out, CLEANUP_NOP);
		long int n = ((thread_data_t*)e.io.in) - data;
		ASSERT(n >= 0 && n < NTHREADS, CLEANUP_NOP);
	}

	ASSERT_OK(_validate_stage(3), CLEANUP_NOP);

	return 0;
}
int full_test(void)
{
	ASSERT_OK(_next_stage(), CLEANUP_NOP);

	int F[1000 * NTHREADS] = {};
	int i;
	itc_equeue_event_mask_t mask = ITC_EQUEUE_EVENT_MASK_NONE;
	ITC_EQUEUE_EVENT_MASK_ADD(mask, ITC_EQUEUE_EVENT_TYPE_IO);
	for(i = 0; i < 1000 * NTHREADS;)
	{
		ASSERT_OK(itc_equeue_wait(sched_token, NULL, NULL), CLEANUP_NOP);
		for(;!itc_equeue_empty(sched_token); i ++)
		{
			itc_equeue_event_t e;
			ASSERT_OK(itc_equeue_take(sched_token, mask, &e), CLEANUP_NOP);
			uintptr_t j = (uintptr_t)e.io.in;
			ASSERT(e.io.in == e.io.out, CLEANUP_NOP);
			ASSERT_OK(F[j - 1], CLEANUP_NOP);
			F[j - 1] = -1;
		}
	}
	ASSERT(itc_equeue_empty(sched_token) == 1, CLEANUP_NOP);
	ASSERT_OK(_validate_stage(4), CLEANUP_NOP);

	return 0;
}
int random_test(void)
{
	ASSERT_OK(_next_stage(), CLEANUP_NOP);

	int F[100 * NTHREADS] = {};
	int i;
	itc_equeue_event_mask_t mask = ITC_EQUEUE_EVENT_MASK_NONE;
	ITC_EQUEUE_EVENT_MASK_ADD(mask, ITC_EQUEUE_EVENT_TYPE_IO);
	for(i = 0; i < 100 * NTHREADS;)
	{
		ASSERT_OK(itc_equeue_wait(sched_token, NULL, NULL), CLEANUP_NOP);
		for(;!itc_equeue_empty(sched_token); i ++)
		{
			itc_equeue_event_t e;
			ASSERT_OK(itc_equeue_take(sched_token, mask, &e), CLEANUP_NOP);
			uintptr_t j = (uintptr_t)e.io.in;
			ASSERT(e.io.in == e.io.out, CLEANUP_NOP);
			ASSERT_OK(F[j - 1], CLEANUP_NOP);
			F[j - 1] = -1;
		}
	}
	ASSERT(itc_equeue_empty(sched_token) == 1, CLEANUP_NOP);
	ASSERT_OK(_validate_stage(5), CLEANUP_NOP);

	return 0;
}
int setup(void)
{
	srand((unsigned)time(NULL));
	uint32_t i;
	for(i = 0; i < NTHREADS; i ++)
	{
		pthread_attr_t attr;
		ASSERT_OK(pthread_cond_init(&data[i].cond, NULL), CLEANUP_NOP);
		ASSERT_OK(pthread_mutex_init(&data[i].mutex, NULL), CLEANUP_NOP);
		ASSERT_OK(pthread_cond_init(&data[i].stage_cond, NULL), CLEANUP_NOP);
		ASSERT_OK(pthread_mutex_init(&data[i].stage_mutex, NULL), CLEANUP_NOP);
		data[i].flag = 0;
		data[i].id = i;
		ASSERT_OK(pthread_attr_init(&attr), CLEANUP_NOP);
		ASSERT_OK(pthread_create(T + i, &attr, thread_main, data + i), CLEANUP_NOP);
	}
	return 0;
}

int teardown(void)
{
	int i;
	for(i = 0; i < NTHREADS; i ++)
	{
		expected_memory_leakage();
		void* ret;
		ASSERT_OK(pthread_cond_destroy(&data[i].cond), CLEANUP_NOP);
		ASSERT_OK(pthread_mutex_destroy(&data[i].mutex), CLEANUP_NOP);
		ASSERT_OK(pthread_cond_destroy(&data[i].stage_cond), CLEANUP_NOP);
		ASSERT_OK(pthread_mutex_destroy(&data[i].stage_mutex), CLEANUP_NOP);
		ASSERT_OK(pthread_join(T[i], &ret), CLEANUP_NOP);
		ASSERT(NULL == ret, CLEANUP_NOP);
	}
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_token),
    TEST_CASE(test_put),
    TEST_CASE(test_writer_wait),
    TEST_CASE(full_test),
    TEST_CASE(random_test)
TEST_LIST_END;
