/**
 * Copyright (C) 2017, Hao Hou
 **/
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
	pipe_t alloc;
	pipe_t dealloc;
} _context_t;

static int _init(uint32_t argc, char const* const* argv, void* data)
{
	(void)argc;
	(void)argv;
	_context_t* ctx = (_context_t*)data;
	ASSERT_RETOK(pipe_t, ctx->alloc = module_require_function("plumber.std", "pool_allocate"), CLEANUP_NOP);
	ASSERT_RETOK(pipe_t, ctx->dealloc = module_require_function("plumber.std", "pool_deallocate"), CLEANUP_NOP);

	return 0;
}

static int _exec(void* data)
{
	_context_t* ctx = (_context_t*)data;
	void* result = NULL;
	ASSERT_OK(pipe_cntl(ctx->alloc, PIPE_CNTL_INVOKE, 32, &result), CLEANUP_NOP);
	ASSERT_PTR(result, CLEANUP_NOP);

	ASSERT_OK(pipe_cntl(ctx->dealloc, PIPE_CNTL_INVOKE, result), CLEANUP_NOP);

	void* result1 = NULL;
	ASSERT_OK(pipe_cntl(ctx->alloc, PIPE_CNTL_INVOKE, 32, &result1), CLEANUP_NOP);
	ASSERT(result == result1, CLEANUP_NOP);

	void* results[1024];
	uint32_t i;
	for(i = 0; i < sizeof(results)/sizeof(results[0]); i ++)
	{
		ASSERT_OK(pipe_cntl(ctx->alloc, PIPE_CNTL_INVOKE, 32, results + i), CLEANUP_NOP);
		ASSERT_PTR(result, CLEANUP_NOP);
		uint32_t j;
		for(j = 0; j < i; j ++)
		    ASSERT(results[i] != results[j], CLEANUP_NOP);
	}

	for(i = 0; i < sizeof(results)/sizeof(results[0]); i ++)
	    memset(results[i], i&0xff, 32);

	for(i = 0; i < sizeof(results)/sizeof(results[0]); i ++)
	{
		uint32_t j = 0;
		for(j = 0; j < 32; j ++)
		    ASSERT(((uint8_t*)results[i])[j] == (i&0xff), CLEANUP_NOP);
	}

	for(i = 0; i < sizeof(results)/sizeof(results[0]); i ++)
	    ASSERT_OK(pipe_cntl(ctx->dealloc, PIPE_CNTL_INVOKE,results[i]), CLEANUP_NOP);;

	ASSERT_OK(pipe_cntl(ctx->dealloc, PIPE_CNTL_INVOKE, result1), CLEANUP_NOP);

	return 0;
}

static int _unload(void* data)
{
	(void)data;
	return 0;
}

SERVLET_DEF = {
	.size = sizeof(_context_t),
	.version = 0x0,
	.desc = "Mempool module test",
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
