/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <constants.h>
#include <pservlet.h>
#include <error.h>
#include <string.h>

#define ASSERT(cond, cleanup) do{\
	if(!(cond)) \
	{\
		LOG_ERROR("Assertion failure `%s'", #cond);\
		cleanup;\
		return -1;\
	}\
} while(0)
#define ASSERT_STREQ(left, right, cleanup) ASSERT(strcmp((left), (right)) == 0, cleanup)
#define ASSERT_PTR(ptr, cleanup) ASSERT((ptr) != NULL, cleanup)
#define ASSERT_OK(status, cleanup) ASSERT((status) != ERROR_CODE(int), cleanup)
#define ASSERT_RETOK(type, ret, cleanup) ASSERT((ret) != ERROR_CODE(type), cleanup)
#define CLEANUP_NOP

typedef struct {
	pipe_t new;
	pipe_t get;
	pipe_t free;

	void* local;
} _context_t;

static inline void* _alloc(uint32_t tid, void* data)
{
	int32_t* ret = ((int32_t*)data) + tid;

	ret[0] = 0;

	return ret;
}

static inline int _dealloc(void* mem, const void* data)
{
	(void) data;

	*(int32_t*)mem = -1;

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* data)
{
	(void)argc;
	(void)argv;
	_context_t* ctx = (_context_t*)data;
	ASSERT_RETOK(pipe_t, ctx->new = module_require_function("plumber.std", "thread_local_new"), CLEANUP_NOP);
	ASSERT_RETOK(pipe_t, ctx->get = module_require_function("plumber.std", "thread_local_get"), CLEANUP_NOP);
	ASSERT_RETOK(pipe_t, ctx->free = module_require_function("plumber.std", "thread_local_free"), CLEANUP_NOP);

	extern int32_t* thread_local_test_buffer;

	ASSERT_OK(pipe_cntl(ctx->new, PIPE_CNTL_INVOKE, _alloc, _dealloc, thread_local_test_buffer, &ctx->local), CLEANUP_NOP);

	return 0;
}

static int _exec(void* data)
{
	_context_t* ctx = (_context_t*)data;
	int32_t* ptr = NULL;
	ASSERT_OK(pipe_cntl(ctx->get, PIPE_CNTL_INVOKE, ctx->local, &ptr), CLEANUP_NOP);
	ASSERT_PTR(ptr, CLEANUP_NOP);

	(*ptr) ++;

	return 0;
}

static int _unload(void* data)
{
	_context_t* ctx = (_context_t*)data;
	ASSERT_OK(pipe_cntl(ctx->free, PIPE_CNTL_INVOKE, ctx->local), CLEANUP_NOP);
	return 0;
}

SERVLET_DEF = {
	.size = sizeof(_context_t),
	.version = 0x0,
	.desc = "thread local module test",
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
