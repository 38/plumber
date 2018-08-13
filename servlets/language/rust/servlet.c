/**
 * Copyright (C) 2018, Hao Hou
 **/

#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#include <pservlet.h>
#include <pstd.h>

/**
 * @brief The callback function which is used as the continuation to call the va_list function
 * @param data The callback additoinal data
 * @param ap The valist pointer
 * @return nothing
 **/
typedef void (*rust_va_list_callback_func_t)(va_list ap, void* data);

/**
 * @brief The wrapper function for rust calling a function with valist pointer
 * @param cont The continuation function
 * @param data The additional data pointer
 * @return nothing
 **/
typedef void (*rust_va_list_wrapper_func_t)(rust_va_list_callback_func_t cont, void* data, ...);

/**
 * @brief The Rust bootstrap function
 * @param argc The number of servelt init arguments
 * @param argv The servlet init argument list
 * @param helper The va_list helper
 * @return The newly created Rust servlet object
 **/
typedef void* (*rust_bootstrap_func_t)(uint32_t argc, char const* const* argv, pstd_type_model_t* tm, const address_table_t* addr_tab, rust_va_list_wrapper_func_t helper);

/**
 * @brief The Rust servlet initialization functon
 * @param self The rust servlet object
 * @param argc The number of arguments
 * @param argv The argument list
 * @param tm The type model object 
 * @return status code
 **/
typedef int (*rust_servlet_init_func_t)(void* self, uint32_t argc, char const* const* argv);

/**
 * @brief The synchronous Rust servlet execute funciton
 * @param self The servlet object
 * @param ti The type instance object
 * @return status code
 **/
typedef int (*rust_servlet_exec_func_t)(void* self, pstd_type_instance_t* ti);

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
 * @param type_inst The type instance 
 * @return The private task data or NULL on error case
 **/
typedef void* (*rust_servlet_async_init_func_t)(void* self, void* handle, pstd_type_instance_t* type_inst);

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
 * @param the type instance
 * @return status  code
 **/
typedef int (*rust_servlet_async_cleanup_func_t)(void*self, void* handle, void* task_data, pstd_type_instance_t* type_inst);

/**
 * @brief The servlet context
 **/
typedef struct {
	void*                             dl_handle;         /*!< The actual rust servlet shared object */
	void*                             rust_servlet_obj;  /*!< The rust servlet object */
	pstd_type_model_t*                type_model;      /*!< The actual type model object */
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
	rust_servlet_async_exec_func_t      exec_func;       /*!< The exec callback */
	void*                               rust_obj;        /*!< The rust object for the async data */
} async_data_t;

static void _va_list_wrapper(rust_va_list_callback_func_t cont, void* data, ...)
{
	va_list ap;
	va_start(ap, data);

	cont(ap, data);

	va_end(ap);
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	if(argc < 2) 
		ERROR_RETURN_LOG(int, "Invalid servlet init string, expected: %s [rust_shared_object] <params>", argv[0]);

	context_t* ctx = (context_t*)ctxmem;

	ctx->dl_handle = NULL;
	ctx->rust_servlet_obj = NULL;

	if(NULL == (ctx->dl_handle = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL)))
		ERROR_RETURN_LOG(int, "Cannot open the shared object %s : %s", argv[1], dlerror());

	rust_bootstrap_func_t bootstrap_func;
	rust_servlet_init_func_t init_func;

	if(NULL == (bootstrap_func = (rust_bootstrap_func_t)dlsym(ctx->dl_handle, "_rs_invoke_bootstrap")))
		ERROR_LOG_GOTO(ERR, "Cannot find symbol _rs_invoke_bootstrap, make sure you are loading a rust servlet binary");

	if(NULL == (init_func = (rust_servlet_init_func_t)dlsym(ctx->dl_handle, "_rs_invoke_init")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find symbol _rs_invoke_init, make sure you are loading a Rust servlet binary");

	if(NULL == (ctx->exec_func = (rust_servlet_exec_func_t)dlsym(ctx->dl_handle, "_rs_invoke_exec")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find symbol _rs_invoke_exec, make sure you are loading a Rust servlet binary");
	
	if(NULL == (ctx->cleanup_func = (rust_servlet_cleanup_func_t)dlsym(ctx->dl_handle, "_rs_invoke_cleanup")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find symbol _rs_invoke_cleanup, make sure you are loading a Rust servlet binary");

	if(NULL == (ctx->async_init_func = (rust_servlet_async_init_func_t)dlsym(ctx->dl_handle, "_rs_invoke_async_init")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find symbol _rs_invoke_async_init, make sure you are loading a Rust servlet binary");
	
	if(NULL == (ctx->async_exec_func = (rust_servlet_async_exec_func_t)dlsym(ctx->dl_handle, "_rs_invoke_async_exec")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find symbol _rs_invoke_async_exec, make sure you are loading a Rust servlet binary");
	
	if(NULL == (ctx->async_cleanup_func = (rust_servlet_async_cleanup_func_t)dlsym(ctx->dl_handle, "_rs_invoke_async_cleanup")))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find symbol _rs_invoke_async_cleanup, make sure you are loading a Rust servlet binary");

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_LOG_GOTO(ERR, "Cannot create type model for the Rust servlet");

	if(NULL == (ctx->rust_servlet_obj = bootstrap_func(argc - 2, argv + 2, ctx->type_model, RUNTIME_ADDRESS_TABLE_SYM, _va_list_wrapper)))
		ERROR_LOG_GOTO(ERR, "Rust servlet bootstrap function returns an error");

	return init_func(ctx->rust_servlet_obj, argc - 2, argv + 2);
ERR:
	if(NULL != ctx->dl_handle) dlclose(ctx->dl_handle);
	return ERROR_CODE(int);
}

static int _exec(void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	pstd_type_instance_t* type_inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	int rc = ctx->exec_func(ctx->rust_servlet_obj, type_inst);

	if(ERROR_CODE(int) == pstd_type_instance_free(type_inst))
	{
		LOG_ERROR("Cannot dispose the type instance for this task");
		rc = ERROR_CODE(int);
	}

	return rc;
}

static int _cleanup(void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	if(ctx->type_model != NULL)
		pstd_type_model_free(ctx->type_model);

	if(ctx->rust_servlet_obj == NULL) return 0;

	return ctx->cleanup_func(ctx->rust_servlet_obj);
}

static int _async_setup(async_handle_t* task_handle, void* task_data, void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;
	async_data_t* async_data = (async_data_t*)task_data;
	
	pstd_type_instance_t* type_inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == (async_data->rust_obj = ctx->async_init_func(ctx->rust_servlet_obj, task_handle, type_inst)))
		ERROR_LOG_GOTO(ERR, "Cannot initialize the async task");

	async_data->exec_func = ctx->async_exec_func;

	return pstd_type_instance_free(type_inst);
ERR:
	pstd_type_instance_free(type_inst);
	return ERROR_CODE(int);
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
	
	pstd_type_instance_t* type_inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	int rc = ctx->async_cleanup_func(ctx->rust_servlet_obj, task_handle, async_data->rust_obj, type_inst);

	if(ERROR_CODE(int) == pstd_type_instance_free(type_inst))
	{
		LOG_ERROR("Cannot dispose the type instance object");
		rc = ERROR_CODE(int);
	}

	return rc;
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
