/**
 * Copyright (C) 2017, Hao Hou
 **/

#if DO_NOT_COMPILE_ITC_MODULE_TEST == 0
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <itc/module_types.h>
#include <module/test/module.h>

#include <utils/log.h>

#include <error.h>

#define TEST_BUFFER_SIZE 4096u

/**
 * @brief the data structure used as the handle for a test pipe
 **/
typedef struct _module_handle_t {
	int type;           /*!< the type of this pipe */
	char* buffer;       /*!< the buffer of this pipe */
	size_t position;    /*!< the current position */
} module_handle_t;

/**
 * @brief the buffer used to mock the request
 **/
static char request_buffer[TEST_BUFFER_SIZE];

/**
 * @brief the buffer used to mock the response
 **/
static char response_buffer[TEST_BUFFER_SIZE];

static int module_init(void* __restrict ctx, uint32_t argc, char const* __restrict const* __restrict argv)
{
	(void)ctx;
	(void)argc;
	(void)argv;
	LOG_DEBUG("Test ITC Module is initialized");
	if(argc > 0) *(const char**)ctx = argv[0];
	else *(const char**)ctx = "__default__";
	return 0;
}

static int module_cleanup(void* __restrict ctx)
{
	(void)ctx;
	LOG_DEBUG("Test ITC Module is finalized");
	return 0;
}

static int allocate(void* __restrict ctx, uint32_t hint, void* __restrict out, void* __restrict in, const void* __restrict args)
{
	(void)ctx;
	(void)args;
	(void)hint;
	char* buf = (char*)calloc(1, TEST_BUFFER_SIZE);
	if(NULL == buf)
	{
		LOG_ERROR("can not initialize the pipe");
		return ERROR_CODE(int);
	}

	module_handle_t* input = (module_handle_t*)in;
	module_handle_t* output = (module_handle_t*)out;

	input->type = 1;
	output->type = 0;
	output->buffer = input->buffer = buf;
	input->position = output->position = 0;
	LOG_DEBUG("pipe has been created!");
	return 0;
}

static int deallocate(void* __restrict ctx, void* __restrict pipe, int error, int purge)
{
	(void) ctx;
	(void) error;
	module_handle_t* handle = (module_handle_t*)pipe;

	if(purge)
	{
		LOG_DEBUG("pipe has been disposed");
		if(handle->buffer != request_buffer && handle->buffer != response_buffer)
		    free(handle->buffer);
		return 0;
	}

	LOG_DEBUG("one side of the pipe dead");
	return 0;
}

static size_t read(void* __restrict ctx, void* __restrict buffer, size_t nbytes, void* __restrict pipe)
{
	(void) ctx;
	module_handle_t* handle = (module_handle_t*)pipe;

	if(handle->type != 1)
	{
		LOG_ERROR("wrong pipe type");
		return ERROR_CODE(size_t);
	}

	if(nbytes + handle->position > TEST_BUFFER_SIZE) nbytes = TEST_BUFFER_SIZE - handle->position;

	memcpy(buffer, handle->buffer + handle->position, nbytes);

	handle->position += nbytes;

	return nbytes;
}

static size_t write(void* __restrict ctx, const void* __restrict buffer, size_t nbytes, void* __restrict pipe)
{
	(void)ctx;

	module_handle_t* handle = (module_handle_t*)pipe;

	if(handle->type != 0)
	{
		LOG_ERROR("wrong pipe type");
		return ERROR_CODE(size_t);
	}

	if(nbytes + handle->position > TEST_BUFFER_SIZE) nbytes = TEST_BUFFER_SIZE - handle->position;

	memcpy(handle->buffer + handle->position, buffer, nbytes);

	handle->position += nbytes;

	return nbytes;
}

static int accept(void* __restrict ctx, const void* __restrict args, void* __restrict in, void* __restrict out)
{
	(void) ctx;
	(void) args;
	module_handle_t* input = (module_handle_t*)in;
	module_handle_t* output = (module_handle_t*)out;

	input->type = 1;
	output->type = 0;
	input->buffer = request_buffer;
	output->buffer = response_buffer;
	input->position = output->position = 0;

	return 0;
}

int module_test_set_request(const void* data, size_t count)
{
	if(count > TEST_BUFFER_SIZE) count = TEST_BUFFER_SIZE;

	memcpy(request_buffer, data, count);

	return 0;
}

const void* module_test_get_response()
{
	return response_buffer;
}

static int _has_unread_data(void* __restrict ctx, void* __restrict data)
{
	(void)ctx;
	module_handle_t* handle = (module_handle_t*) data;
	if(NULL == handle)
	{
		LOG_ERROR("invalid arguments");
		return ERROR_CODE(int);
	}

	if(handle->type != 1)
	{
		LOG_ERROR("cannot perform has_unread call on a output pipe");
		return ERROR_CODE(int);
	}

	return TEST_BUFFER_SIZE > handle->position;
}

static int _cntl(void* __restrict ctx, void* __restrict handle, uint32_t opcode, va_list args)
{
	(void) ctx;
	(void) handle;
	if((opcode & 0xffff) == 0)
	{
		typedef void (*trap_func_t)(int);
		trap_func_t func;
		func = va_arg(args, trap_func_t);
		func((int)((itc_module_get_handle_flags(handle) >> 16) + 1) * 35);
		return 0;
	}
	return ERROR_CODE(int);
}

static const char* _get_path(void* __restrict ctx, char* buf, size_t sz)
{
	(void) ctx;
	snprintf(buf, sz, "%s", *(const char**)ctx);
	return buf;
}

static uint32_t _get_opcode(void* __restrict ctx, const char* name)
{
	(void)ctx;
	(void)name;
	return 0x0;
}

static int _fork(void* __restrict ctx, void* __restrict dest, void* __restrict src, const void* __restrict args)
{
	(void) ctx;
	(void) args;
	module_handle_t* dh = (module_handle_t*)dest;
	module_handle_t* sh = (module_handle_t*)src;

	dh->type = 1;
	dh->position = 0;
	dh->buffer = sh->buffer;

	return 0;
}

itc_module_t module_test_module_def = {
	.mod_prefix = "pipe.test",
	.handle_size = sizeof(module_handle_t),
	.context_size = sizeof(const char*),
	.module_init = module_init,
	.module_cleanup = module_cleanup,
	.allocate = allocate,
	.deallocate = deallocate,
	.read = read,
	.write = write,
	.accept = accept,
	.has_unread_data = _has_unread_data,
	.cntl = _cntl,
	.get_path = _get_path,
	.get_opcode = _get_opcode,
	.fork = _fork
};

#endif /* DO_NOT_COMPILE_ITC_MODULE_TEST */
