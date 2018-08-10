/**
 * Copyright (C) 2018, Hao Hou
 **/

#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#include <pservlet.h>

/**
 * @brief The Rust bootstrap function
 * @param argc The number of servelt init arguments
 * @param argv The servlet init argument list
 * @return The newly created Rust servlet object
 **/
typedef void* (*rust_bootstrap_func_t)(uint32_t argc, char const* const* argv, const address_table_t* addr_tab);

/**
 * @brief The Rust servlet initialization functon
 * @param self The rust servlet object
 * @param argc The number of arguments
 * @param argv The argument list
 * @return status code
 **/
typedef int (*rust_servlet_init_func_t)(void* self, uint32_t argc, char const* const* argv);

/**
 * @brief The synchronous Rust servlet execute funciton
 * @param self The servlet object
 * @return status code
 **/
typedef int (*rust_servlet_exec_func_t)(void* self);

/**
 * @brief The asynchronous Rust servlet execute function
 * @param self The servlet object
 * @return status code
 **/
typedef int (*rust_servlet_cleanup_func_t)(void* self);

/**
 * @brief The asynchronous Rust servlet task init function
 * @param self The servlet object
 * @param handle The task handle
 * @return The private task data or NULL on error case
 **/
typedef void* (*rust_servlet_async_init_func_t)(void* self, void* handle);

/**
 * @brief The async Rust servlet task execution
 * @param handle The task handle
 * @param task_data the task private data
 * @return status code
 **/
typedef int (*rust_servlet_async_exec_func_t)(void* handle, void* task_data);

/**
 * @brief The async Rust servlet cleanup 
 * @param self The servlet object
 * @param handle The task handle
 * @param task_data The task data
 * @return status  code
 **/
typedef int (*rust_servlet_async_cleanup_func_t)(void*self, void* handle, void* task_data);

/**
 * @brief The servlet context
 **/
typedef struct {
	void*                             dl_handle;         /*!< The actual rust servlet shared object */
	void*                             rust_servlet_obj;  /*!< The rust servlet object */
	rust_servlet_exec_func_t          exec_func;         /*!< The execute function for sync servlet */
	rust_servlet_cleanup_func_t       cleanup_func;      /*!< The cleanup callback */
	rust_servlet_async_init_func_t    async_init_func;   /*!< The async init callback */
	rust_servlet_async_exec_func_t    async_exec_func;   /*!< The async exec callback */
	rust_servlet_async_cleanup_func_t async_cleanup_func;/*!< The async cleanup callback */
} context_t;

/**
 * @brief The async data
 **/
typedef struct {
	rust_servlet_async_exec_func_t      exec_func;      /*!< The exec callback */
	void*                               rust_obj;        /*!< The rust object for the async data */
} async_data_t;


static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	if(argc < 2) 
		ERROR_RETURN_LOG(int, "Invalid servlet init string, expected: %s [rust_shared_object] <params>", argv[0]);

	context_t* ctx = (context_t*)ctxmem;

	ctx->dl_handle = NULL;
	ctx->rust_servlet_obj = NULL;

	if(NULL == (ctx->dl_handle = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL)))
		ERROR_RETURN_LOG(int, "Cannot open the shared object %s", argv[1]);

	rust_bootstrap_func_t bootstrap_func;
	rust_servlet_init_func_t init_func;

	if(NULL == (bootstrap_func = (rust_bootstrap_func_t)dlsym(ctx->dl_handle, "_rs_invoke_bootstrap")))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot find symbol _rs_invoke_bootstrap, make sure you are loading a rust servlet binary");

	if(NULL == (init_func = (rust_servlet_init_func_t)dlsym(ctx->dl_handle, "_rs_invoke_init")))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot find symbol _rs_invoke_init, make sure you are loading a Rust servlet binary");

	if(NULL == (ctx->exec_func = (rust_servlet_exec_func_t)dlsym(ctx->dl_handle, "_rs_invoke_exec")))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot find symbol _rs_invoke_exec, make sure you are loading a Rust servlet binary");
	
	if(NULL == (ctx->cleanup_func = (rust_servlet_exec_func_t)dlsym(ctx->dl_handle, "_rs_invoke_cleanup")))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot find symbol _rs_invoke_cleanup, make sure you are loading a Rust servlet binary");

	if(NULL == (ctx->async_init_func = (rust_servlet_async_init_func_t)dlsym(ctx->dl_handle, "_rs_invoke_async_init")))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot find symbol _rs_invoke_async_init, make sure you are loading a Rust servlet binary");
	
	if(NULL == (ctx->async_exec_func = (rust_servlet_async_exec_func_t)dlsym(ctx->dl_handle, "_rs_invoke_async_exec")))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot find symbol _rs_invoke_async_exec, make sure you are loading a Rust servlet binary");
	
	if(NULL == (ctx->async_cleanup_func = (rust_servlet_async_cleanup_func_t)dlsym(ctx->dl_handle, "_rs_invoke_async_cleanup")))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot find symbol _rs_invoke_async_cleanup, make sure you are loading a Rust servlet binary");

	if(NULL == (ctx->rust_servlet_obj = bootstrap_func(argc - 2, argv + 2, RUNTIME_ADDRESS_TABLE_SYM)))
		ERROR_LOG_GOTO(ERR, "Rust servlet bootstrap function returns an error");

	return init_func(ctx->rust_servlet_obj, argc - 2, argv + 2);
ERR:
	if(NULL != ctx->dl_handle) dlclose(ctx->dl_handle);
	return ERROR_CODE(int);
}

static int _exec(void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	return ctx->exec_func(ctx->rust_servlet_obj);
}

static int _cleanup(void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	if(ctx->rust_servlet_obj == NULL) return 0;

	return ctx->cleanup_func(ctx->rust_servlet_obj);
}

static int _async_setup(async_handle_t* task_handle, void* task_data, void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;
	async_data_t* async_data = (async_data_t*)task_data;

	if(NULL == (async_data->rust_obj = ctx->async_init_func(ctx->rust_servlet_obj, task_handle)))
		ERROR_RETURN_LOG(int, "Cannot initialize the async task");

	async_data->exec_func = ctx->async_exec_func;

	return 0;
}

static int _async_exec(async_handle_t* task_handle, void* task_data)
{
	async_data_t* async_data = (async_data_t*)task_data;

	return async_data->exec_func(task_handle, async_data->rust_obj);
}

static int _async_cleanup(async_handle_t* task_handle, void* task_data, void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;
	async_data_t* async_data = (async_data_t*)task_data;

	return ctx->async_cleanup_func(ctx->rust_servlet_obj, task_handle, async_data->rust_obj);
}

SERVLET_DEF = {
	.desc    = "The Rust Servlet Loader",
	.version = 0x0,
	.size    = sizeof(context_t),
	.init    = _init,
	.exec    = _exec,
	.unload  = _cleanup,
	.async_buf_size = sizeof(async_data_t),
	.async_setup = _async_setup,
	.async_exec = _async_exec,
	.async_cleanup = _async_cleanup
};
