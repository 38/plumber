/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>

#define N 10240
static int status[N];

void* test_multiple_request_copy_func(const void* ptr)
{
	const int* p = (const int*)ptr;
	status[(p - status) + N / 2] = *p;

	return status + N / 2 + (p - status) ;
}

int test_multiple_request_free_func(void* ptr)
{
	int* p = (int*)ptr;
	p[0] = 0;
	return 0;
}

int test_multiple_request(void)
{
	sched_rscope_t* scope1 = NULL;
	sched_rscope_t* scope[10];
#define M (sizeof(scope) / sizeof(scope[0]))
	size_t i;

	for(i = 0; i < M; i ++)
	{
		scope[i] = sched_rscope_new();
		ASSERT_PTR(scope, goto ERR);
	}
	ASSERT_PTR(scope1 = sched_rscope_new(), goto ERR);

	for(i = 0; i < 100; i ++)
	{
		runtime_api_scope_entity_t ptr = {
			.data = status + i,
			.copy_func = test_multiple_request_copy_func,
			.free_func = test_multiple_request_free_func,
		};
		status[i] = -1;
		runtime_api_scope_token_t token;
		ASSERT_RETOK(runtime_api_scope_token_t, token = sched_rscope_add(scope1, &ptr), goto ERR);
	}

	ASSERT_OK(sched_rscope_free(scope1), goto ERR);
	scope1 = NULL;

	for(i = 0; i < N / 2; i ++)
	{
		runtime_api_scope_entity_t ptr = {
			.data = status + i,
			.copy_func = test_multiple_request_copy_func,
			.free_func = test_multiple_request_free_func,
		};
		status[i] = (int)(i % M);
		runtime_api_scope_token_t token;
		sched_rscope_copy_result_t result;
		ASSERT_RETOK(runtime_api_scope_token_t, token = sched_rscope_add(scope[i%M], &ptr), goto ERR);
		ASSERT_OK(sched_rscope_copy(scope[i%M], token, &result), goto ERR);
		ASSERT(status + i + N / 2 == result.ptr, goto ERR);
		ASSERT_RETOK(runtime_api_scope_token_t, result.token, goto ERR);
		ASSERT(token < N, goto ERR);
		ASSERT(result.token < N, goto ERR);
	}

	for(i = 0; i < N; i ++)
	    ASSERT((int)(i % M) == status[i], goto ERR);

	for(i = 0; i < M; i ++)
	    ASSERT_OK(sched_rscope_free(scope[i]), CLEANUP_NOP);

	for(i = 0; i < N; i ++)
	    ASSERT(0 == status[i], CLEANUP_NOP);
	return 0;
ERR:
	for(i = 0; i < M; i ++)
	{
		if(NULL != scope[i]) sched_rscope_free(scope[i]);
	}
	if(scope1 != NULL) sched_rscope_free(scope1);
	return ERROR_CODE(int);
}
typedef struct {
	char begin, end;
} stream_object_t;

typedef struct {
	stream_object_t* obj;
	char current;
} stream_handle_t;

static inline void* _stream_obj_copy(const void* ptr)
{
	stream_object_t* ret = (stream_object_t*)malloc(sizeof(*ret));
	*ret = *(const stream_object_t*)ptr;
	return ret;
}

static inline int _stream_obj_free(void* ptr)
{
	if(NULL == ptr) return ERROR_CODE(int);
	free(ptr);
	return 0;
}

static inline void* _stream_obj_open(const void* ptr)
{
	if(NULL == ptr) return NULL;
	stream_handle_t* ret = (stream_handle_t*)malloc(sizeof(*ret));
	ret->obj = (stream_object_t*)ptr;
	ret->current = ret->obj->begin;

	return ret;
}

static inline int _stream_obj_close(void* handle)
{
	if(NULL == handle) return ERROR_CODE(int);
	free(handle);
	return 0;
}

static inline int _stream_obj_eos(const void* handle)
{
	if(NULL == handle) return ERROR_CODE(int);
	stream_handle_t* hand = (stream_handle_t*)handle;
	return hand->obj->end <= hand->current;
}

static inline size_t _stream_obj_read(void* handle, void* buffer, size_t sz)
{
	if(NULL == handle || NULL == buffer)
	    return ERROR_CODE(size_t);

	size_t ret;
	char* result = (char*)buffer;
	stream_handle_t* hand = (stream_handle_t*)handle;
	for(ret = 0; hand->current < hand->obj->end && ret < sz; hand->current ++, ret ++)
	    result[ret] = hand->current;

	return ret;
}

int test_stream_interface(void)
{
	runtime_api_scope_token_t t1, t2;
	sched_rscope_t* scope = sched_rscope_new();
	ASSERT_PTR(scope, CLEANUP_NOP);
	stream_object_t* obj1 = (stream_object_t*)malloc(sizeof(stream_object_t));
	ASSERT_PTR(obj1, CLEANUP_NOP);
	obj1->begin = 'a';
	obj1->end = 'z' + 1;
	runtime_api_scope_entity_t p1 = {
		.data = obj1,
		.copy_func = _stream_obj_copy,
		.free_func = _stream_obj_free,
		.open_func = _stream_obj_open,
		.close_func = _stream_obj_close,
		.eos_func = _stream_obj_eos,
		.read_func = _stream_obj_read
	};
	ASSERT_RETOK(runtime_api_scope_token_t, t1 = sched_rscope_add(scope, &p1), CLEANUP_NOP);

	stream_object_t* obj2 = (stream_object_t*)malloc(sizeof(stream_object_t));
	ASSERT_PTR(obj2, CLEANUP_NOP);
	obj2->begin = 'A';
	obj2->end = 'Z' + 1;
	runtime_api_scope_entity_t p2 = {
		.data = obj2,
		.copy_func = _stream_obj_copy,
		.free_func = _stream_obj_free,
		.open_func = _stream_obj_open,
		.close_func = _stream_obj_close,
		.eos_func = _stream_obj_eos,
		.read_func = _stream_obj_read
	};
	ASSERT_RETOK(runtime_api_scope_token_t, t2 = sched_rscope_add(scope, &p2), CLEANUP_NOP);

	sched_rscope_stream_t* s1[2], *s2[2];
	ASSERT_PTR(s1[0] = sched_rscope_stream_open(t1), CLEANUP_NOP);
	ASSERT_PTR(s2[0] = sched_rscope_stream_open(t2), CLEANUP_NOP);
	ASSERT_PTR(s1[1] = sched_rscope_stream_open(t1), CLEANUP_NOP);
	ASSERT_PTR(s2[1] = sched_rscope_stream_open(t2), CLEANUP_NOP);

	char b1[11] = {}, b2[11] = {};
	char bb1[6] = {}, bb2[6] = {};

	/* Round 1 */
	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));
	memset(bb1, 0, sizeof(bb1));
	memset(bb2, 0, sizeof(bb2));
	ASSERT(10 == sched_rscope_stream_read(s1[0], b1, 10), CLEANUP_NOP);
	ASSERT(0 == strcmp(b1, "abcdefghij"), CLEANUP_NOP);
	ASSERT(10 == sched_rscope_stream_read(s2[0], b2, 10), CLEANUP_NOP);
	ASSERT(0 == strcmp(b2, "ABCDEFGHIJ"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s1[1], bb1, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb1, "abcde"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s2[1], bb2, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb2, "ABCDE"), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s1[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s1[1]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s2[1]), CLEANUP_NOP);

	/* Round 2 */
	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));
	memset(bb1, 0, sizeof(bb1));
	memset(bb2, 0, sizeof(bb2));
	ASSERT(10 == sched_rscope_stream_read(s1[0], b1, 10), CLEANUP_NOP);
	ASSERT(0 == strcmp(b1, "klmnopqrst"), CLEANUP_NOP);
	ASSERT(10 == sched_rscope_stream_read(s2[0], b2, 10), CLEANUP_NOP);
	ASSERT(0 == strcmp(b2, "KLMNOPQRST"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s1[1], bb1, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb1, "fghij"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s2[1], bb2, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb2, "FGHIJ"), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s1[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s1[1]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s2[1]), CLEANUP_NOP);

	/* Round 3 */
	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));
	memset(bb1, 0, sizeof(bb1));
	memset(bb2, 0, sizeof(bb2));
	ASSERT(6 == sched_rscope_stream_read(s1[0], b1, 10), CLEANUP_NOP);
	ASSERT(0 == strcmp(b1, "uvwxyz"), CLEANUP_NOP);
	ASSERT(6 == sched_rscope_stream_read(s2[0], b2, 10), CLEANUP_NOP);
	ASSERT(0 == strcmp(b2, "UVWXYZ"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s1[1], bb1, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb1, "klmno"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s2[1], bb2, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb2, "KLMNO"), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s1[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s1[1]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s2[1]), CLEANUP_NOP);

	/* Round 4 */
	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));
	memset(bb1, 0, sizeof(bb1));
	memset(bb2, 0, sizeof(bb2));
	ASSERT(0 == sched_rscope_stream_read(s1[0], b1, 10), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_read(s2[0], b2, 10), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s1[1], bb1, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb1, "pqrst"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s2[1], bb2, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb2, "PQRST"), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s1[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s1[1]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s2[1]), CLEANUP_NOP);

	/* Round 5 */
	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));
	memset(bb1, 0, sizeof(bb1));
	memset(bb2, 0, sizeof(bb2));
	ASSERT(0 == sched_rscope_stream_read(s1[0], b1, 10), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_read(s2[0], b2, 10), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s1[1], bb1, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb1, "uvwxy"), CLEANUP_NOP);
	ASSERT(5 == sched_rscope_stream_read(s2[1], bb2, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb2, "UVWXY"), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s1[1]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_eos(s2[1]), CLEANUP_NOP);

	/* Round 6 */
	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));
	memset(bb1, 0, sizeof(bb1));
	memset(bb2, 0, sizeof(bb2));
	ASSERT(0 == sched_rscope_stream_read(s1[0], b1, 10), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_read(s2[0], b2, 10), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_read(s1[1], bb1, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb1, "z"), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_read(s2[1], bb2, 5), CLEANUP_NOP);
	ASSERT(0 == strcmp(bb2, "Z"), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s1[0]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s1[1]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[1]), CLEANUP_NOP);

	/* Round 7 */
	memset(b1, 0, sizeof(b1));
	memset(b2, 0, sizeof(b2));
	memset(bb1, 0, sizeof(bb1));
	memset(bb2, 0, sizeof(bb2));
	ASSERT(0 == sched_rscope_stream_read(s1[0], b1, 10), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_read(s2[0], b2, 10), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_read(s1[1], bb1, 5), CLEANUP_NOP);
	ASSERT(0 == sched_rscope_stream_read(s2[1], bb2, 5), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s1[0]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s1[1]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[0]), CLEANUP_NOP);
	ASSERT(1 == sched_rscope_stream_eos(s2[1]), CLEANUP_NOP);


	ASSERT_OK(sched_rscope_stream_close(s1[0]), CLEANUP_NOP);
	ASSERT_OK(sched_rscope_stream_close(s2[0]), CLEANUP_NOP);
	ASSERT_OK(sched_rscope_stream_close(s1[1]), CLEANUP_NOP);
	ASSERT_OK(sched_rscope_stream_close(s2[1]), CLEANUP_NOP);
	ASSERT_OK(sched_rscope_free(scope), CLEANUP_NOP);
	return 0;
}

int setup(void)
{
	return sched_rscope_init_thread();
}

int teardown(void)
{
	return sched_rscope_finalize_thread();
}

TEST_LIST_BEGIN
    TEST_CASE(test_multiple_request),
    TEST_CASE(test_stream_interface)
TEST_LIST_END;
