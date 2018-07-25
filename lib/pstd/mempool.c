/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <error.h>
#include <pservlet.h>
#include <pstd/mempool.h>

void* pstd_mempool_alloc(uint32_t size)
{
	static pipe_t allocator = ERROR_CODE(pipe_t);

	if(allocator == ERROR_CODE(pipe_t))
	{
		allocator = module_require_function("plumber.std", "pool_allocate");
		if(ERROR_CODE(pipe_t) == allocator)
			ERROR_PTR_RETURN_LOG("Cannot get the service module method reference for plumber.std.pool_allocate, PSSM is not installed?");
	}

	void* ret;
	int rc = pipe_cntl(allocator, PIPE_CNTL_INVOKE, size, &ret);
	if(ERROR_CODE(int) == rc)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory from memory pool");

	return ret;
}

int pstd_mempool_free(void* mem)
{
	if(NULL == mem) ERROR_RETURN_LOG(int, "Invalid arguments");

	static pipe_t deallocator = ERROR_CODE(pipe_t);

	if(deallocator == ERROR_CODE(pipe_t))
	{
		deallocator = module_require_function("plumber.std", "pool_deallocate");

		if(ERROR_CODE(pipe_t) == deallocator)
			ERROR_RETURN_LOG(int, "Cannot get the service module method reference for plumber.std.pool_deallocate, PSSM is not installed?");
	}

	return pipe_cntl(deallocator, PIPE_CNTL_INVOKE, mem);
}

void* pstd_mempool_page_alloc()
{
	static pipe_t pipe = ERROR_CODE(pipe_t);

	if(ERROR_CODE(pipe_t) == pipe && ERROR_CODE(pipe_t) == (pipe = module_require_function("plumber.std", "page_allocate")))
		ERROR_PTR_RETURN_LOG("Cannot get the service module method refernece for plumber.std.page_allocate, PSSM may not be loaded");

	void* ret;
	if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_INVOKE, &ret) || NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory from the page memory pool");

	return ret;
}

int pstd_mempool_page_dealloc(void* page)
{
	static pipe_t pipe = ERROR_CODE(pipe_t);

	if(ERROR_CODE(pipe_t) == pipe && ERROR_CODE(pipe_t) == (pipe = module_require_function("plumber.std", "page_deallocate")))
		ERROR_RETURN_LOG(int, "Cannot get the service module method reference for plumber.std.page_deallocate, PSSM may not be loaded");

	return pipe_cntl(pipe, PIPE_CNTL_INVOKE, page);
}
