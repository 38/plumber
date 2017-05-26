/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <constants.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <link.h>

#include <error.h>

#include <utils/log.h>
#include <utils/vector.h>
#include <utils/string.h>

#include <runtime/api.h>

#include <itc/itc.h>

#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <lang/bytecode.h>
#include <lang/prop.h>

/** @brief prevous declearator for the address table */
extern runtime_api_address_table_t runtime_api_address_table;

/** @brief the search path list */
static vector_t* _search_paths;

/**
 * @brief search for a library
 * @return the path to the servlet, NULL when not found
 **/
static inline const char * _search_for_binary(const char* binary)
{
	static char buffer[PATH_MAX + 1];
	size_t i;

	for(i = 0; i < vector_length(_search_paths); i ++)
	{
		const char * const * cell = VECTOR_GET_CONST(const char *, _search_paths, i);
		if(NULL == cell || NULL == *cell) continue;

		string_buffer_t strbuf;

		string_buffer_open(buffer, sizeof(buffer), &strbuf);

		string_buffer_append(*cell, &strbuf);

		const char *begin, *end;
		for(begin = end = binary; ; end ++)
		    if(*end == '/' || *end == 0 )
		    {
			    if(begin < end)
			    {
				    if(*end != 0)
				        string_buffer_append("/", &strbuf);
				    else
				        string_buffer_append("/" RUNTIME_SERVLET_FILENAME_PREFIX, &strbuf);

				    string_buffer_append_range(begin, end, &strbuf);
			    }
			    if(*end == 0) break;
			    begin = end + 1;
		    }

		if(begin < end)
		{
			string_buffer_append(RUNTIME_SERVLET_FILENAME_SUFFIX, &strbuf);
			string_buffer_close(&strbuf);

			LOG_DEBUG("Looking for shared object %s", buffer);

			if(access(buffer, F_OK) == 0)
			{
				LOG_DEBUG("Found shared object %s", buffer);
				return buffer;
			}
		}
	}

	return NULL;
}

static inline int _set_prop(const lang_prop_callback_vector_t* cb, const void* data, uint32_t nsect, const uint32_t* symbol, lang_prop_type_t type, const void* buffer)
{
	(void) data;
	if(NULL == symbol || NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(nsect == 1 && strcmp(lang_prop_get_symbol_string(cb, symbol[0]), "path") == 0)
	{
		if(type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mismatch");
		const char* value = (const char*)buffer;
		runtime_servlet_clear_search_path();
		char buffer[PATH_MAX + 1];
		size_t length = 0;
		for(;;value ++)
		{
			if(*value == ':' || *value == 0)
			{
				buffer[length] = 0;
				if(length > 0)
				{
					if(runtime_servlet_append_search_path(buffer) == ERROR_CODE(int))
					    LOG_WARNING("Cannot append search path \"%s\" to the search path list", buffer);
					else
					    LOG_INFO("New search path: \"%s\"", buffer);
				}
				length = 0;
				if(*value == ':') continue;
				else break;
			}
			if(length < PATH_MAX) buffer[length++] = *value;
		}
	}
	else
	{
		LOG_WARNING("Undefined property symbol %s", lang_prop_get_symbol_string(cb, symbol[0]));
		return 0;
	}

	return 1;
}

static inline int _get_prop(const lang_prop_callback_vector_t* cb, const void* data, uint32_t nsect, const uint32_t* symbol, lang_prop_type_t type, void* buffer)
{
	(void) data;
	if(NULL == symbol || NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(nsect == 1 && strcmp(lang_prop_get_symbol_string(cb, symbol[0]), "path") == 0)
	{
		if(type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mismatch");

		size_t i, required_size = 0;

		for(i = 0; i < vector_length(_search_paths); i ++)
		{
			const char* path = *VECTOR_GET_CONST(const char *, _search_paths, i);
			if(NULL == path) ERROR_RETURN_LOG(int, "Cannot get the search path");
			/* Because we can have either a : in the middle of the string or the \0 at the end */
			required_size += strlen(path) + 1;
		}

		char* bufmem = (char*)malloc(required_size);
		if(NULL == bufmem)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the result");

		string_buffer_t strbuf;
		string_buffer_open(bufmem, required_size, &strbuf);

		int first = 1;

		for(i = 0; i < vector_length(_search_paths); i ++)
		{
			const char* path = *VECTOR_GET_CONST(const char *, _search_paths, i);
			if(NULL == path)
			{
				free(bufmem);
				ERROR_RETURN_LOG(int, "Cannot get the search path");
			}

			if(first)
			    first = 0;
			else
			    string_buffer_append(":", &strbuf);

			string_buffer_append(path, &strbuf);
		}

		string_buffer_close(&strbuf);

		*(const char**)buffer = bufmem;

		return 1;
	}

	LOG_WARNING("Undefined property symbol %s", lang_prop_get_symbol_string(cb, symbol[0]));

	return 0;
}

int runtime_servlet_init()
{
	if(NULL == (_search_paths = vector_new(sizeof(char*), RUNTIME_SERVLET_SEARCH_PATH_INIT_SIZE)))
	    ERROR_RETURN_LOG(int, "Cannot create servlet search path list");

	lang_prop_callback_t cb = {
		.param = NULL,
		.get   = _get_prop,
		.set   = _set_prop,
		.symbol_prefix = "runtime.servlet"
	};

	if(ERROR_CODE(int) == lang_prop_register_callback(&cb))
	    ERROR_RETURN_LOG(int, "Cannot register callback for the runtime prop callback");

	return 0;
}

int runtime_servlet_finalize()
{
	runtime_servlet_clear_search_path();
	if(NULL != _search_paths) vector_free(_search_paths);
	return 0;
}

int runtime_servlet_clear_search_path()
{
	size_t i;
	for(i = 0; i < vector_length(_search_paths); i ++)
	{
		char* path;
		if(NULL != (path = *VECTOR_GET_CONST(char *, _search_paths, i)))
		    free(path);
	}
	vector_clear(_search_paths);
	return 0;
}

int runtime_servlet_append_search_path(const char* path)
{
	if(NULL == path)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	size_t size = strlen(path);
	if(size > PATH_MAX)
	{
		LOG_WARNING("the path is longer than PATH_MAX, truncated");
		size = PATH_MAX;
	}
	char* buf = (char*)malloc(size + 1);

	if(NULL == buf)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory");

	memcpy(buf, path, size);
	buf[size] = 0;

	vector_t* ret = vector_append(_search_paths, &buf);
	if(NULL == ret)
	{
		free(buf);
		ERROR_RETURN_LOG(int, "Cannot apppend pth to the servlet search list");
	}
	_search_paths = ret;

	LOG_DEBUG("Servlet binary search path %s has been added to the list", path);
	return 0;
}

size_t runtime_servlet_num_search_path()
{
	return vector_length(_search_paths);
}

const char * const * runtime_servlet_search_paths()
{
	return VECTOR_GET_CONST(const char*, _search_paths, 0);
}

const char* runtime_servlet_find_binary(const char* servlet)
{
	return _search_for_binary(servlet);
}

runtime_servlet_t* runtime_servlet_new(const runtime_servlet_binary_t* binary, uint32_t argc, char const* const* argv)
{
	runtime_servlet_t* ret = NULL;
	runtime_pdt_t* pdt = NULL;
	runtime_task_t* init_task = NULL;
	char** arguments = NULL;
	uint32_t i;

	if(NULL == binary || argc == 0 || NULL == argv) ERROR_LOG_GOTO(ERR, "Invalid arguments");

	/* Allocate memory for the servlet instance */
	if(NULL == (ret = (runtime_servlet_t*)malloc(sizeof(runtime_servlet_t) + binary->define->size)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot allocate memory for the servlet instance of %s", binary->name);

	ret->bin = binary;

	/* Allocate PDT for the servlet instance */
	if(NULL == (ret->pdt = pdt = runtime_pdt_new()))
	    ERROR_LOG_GOTO(ERR, "cannot allocate PDT for the servlet instance of %s", binary->name);

	if(ERROR_CODE(runtime_api_pipe_t) == (ret->sig_null = runtime_pdt_insert(pdt, "__null__", RUNTIME_API_PIPE_OUTPUT, NULL)))
		ERROR_LOG_GOTO(ERR, "Cannot insert default __null__ pipe to servlet PDT");

	if(ERROR_CODE(runtime_api_pipe_t) == (ret->sig_error = runtime_pdt_insert(pdt, "__error__", RUNTIME_API_PIPE_OUTPUT, NULL)))
		ERROR_LOG_GOTO(ERR, "Cannot insert default __error__ pipe to servlet PDT");

	/* Copy the argument list */
	ret->argc = argc;
	if(NULL == (ret->argv = arguments = (char**)malloc(sizeof(char*) * argc)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "cannot allocate argument list for the servlet instance of %s", binary->name);

	memset(arguments, 0, sizeof(char*) * argc);
	for(i = 0; i < argc; i ++)
	{
		size_t len = strlen(argv[i]);
		arguments[i] = (char*)malloc(len + 1);
		if(NULL == arguments[i])
		    ERROR_LOG_GOTO(ERR, "cannot allocate memory for the %d-th argument", i);
		memcpy(arguments[i], argv[i], len + 1);
	}

	ret->task_pool = NULL;
	ret->owner = NULL;

	/* Invoke the init task */
	if(NULL != binary->define->init)
	{
		if(NULL == (init_task = runtime_task_new(ret, RUNTIME_TASK_FLAG_ACTION_INIT)))
		    ERROR_LOG_GOTO(ERR, "cannot create init task for servlet instance of %s", binary->name);

		if(runtime_task_start(init_task) == ERROR_CODE(int))
		    ERROR_LOG_GOTO(ERR, "init task for servlet instance of %s has failed", binary->name);
	}

	if(runtime_task_free(init_task) == ERROR_CODE(int))
	    LOG_WARNING("cannot dispose executed init task for the servlet instance %s, memory leak possible!", binary->name);

	LOG_INFO("Servlet instance of %s has been created with arguments", binary->name);

	for(i = 0; i < argc; i ++)
	    LOG_INFO("\targv[%u] = \t%s", i, arguments[i]);

	return ret;
ERR:
	if(pdt != NULL) runtime_pdt_free(pdt);
	if(init_task != NULL) runtime_task_free(init_task);
	if(ret != NULL) free(ret);
	if(arguments != NULL)
	{
		for(i = 0; i < argc; i ++)
		    if(arguments[i] != NULL) free(arguments[i]);
		free(arguments);
	}
	return NULL;
}

int runtime_servlet_free(runtime_servlet_t* servlet)
{
	int rc = 0;

	if(NULL == servlet) ERROR_RETURN_LOG(int, "Invalid arguments");

	/* Call the servlet unload task */
	if(NULL != servlet->bin->define->unload)
	{
		runtime_task_t* task = runtime_task_new(servlet, RUNTIME_TASK_FLAG_ACTION_UNLOAD);

		if(NULL == task)
		{
			LOG_WARNING("could not create unload task for servlet instance of %s", servlet->bin->name);
			rc = ERROR_CODE(int);
		}
		else if(runtime_task_start(task) == ERROR_CODE(int))
		{
			LOG_WARNING("could not exeute the unload task for servlet instance of %s", servlet->bin->name);
			rc = ERROR_CODE(int);
		}
		else
		    LOG_DEBUG("unload task for servlet instance of %s terminates with a success status code", servlet->bin->name);

		if(NULL != task && runtime_task_free(task) == ERROR_CODE(int))
		    LOG_WARNING("could not dispose the cleanup task for servlet instance of %s", servlet->bin->name);
	}

	if(servlet->pdt != NULL && runtime_pdt_free(servlet->pdt) == ERROR_CODE(int))
	{
		rc = ERROR_CODE(int);
		LOG_WARNING("could not free the PDT for the servlet instance of %s", servlet->bin->name);
	}

	if(servlet->argv != NULL)
	{
		size_t i;
		for(i = 0; i < servlet->argc; i ++)
		    if(NULL != servlet->argv[i])
		        free(servlet->argv[i]);
		free(servlet->argv);
	}

	if(servlet->task_pool != NULL && mempool_objpool_free(servlet->task_pool) == ERROR_CODE(int))
	    LOG_WARNING("Cannot dispose the task memory pool");

	if(rc != ERROR_CODE(int))
	    LOG_DEBUG("Servlet instance of %s has been unloaded successfully", servlet->bin->name);

	free(servlet);

	return rc;
}

runtime_servlet_binary_t* runtime_servlet_binary_load(const char* path, const char* name)
{
	void* dl_handler = NULL;
	runtime_api_servlet_def_t* def = NULL;
	runtime_servlet_binary_t* ret = NULL;
	runtime_api_address_table_t** addrtab = NULL;
	string_buffer_t strbuf;

	if(NULL == path) ERROR_LOG_GOTO(ERR, "Invalid arguments");

	dl_handler = dlopen(path, RTLD_LAZY | RTLD_LOCAL);

	if(NULL == dl_handler)
	    ERROR_LOG_GOTO(ERR, "Cannot open shared object %s: %s", path, dlerror());

	/* Get the metadata struct from the servlet */
	if(NULL == (def = (runtime_api_servlet_def_t*)dlsym(dl_handler, RUNTIME_SERVLET_DEFINE_STR)))
	    ERROR_LOG_GOTO(ERR,"Cannot find metadata in the servlet,"
	                       " make sure symbol `%s' has been defined in servlet (dlerror: %s) ",
	                   RUNTIME_SERVLET_DEFINE_STR, dlerror());

	LOG_INFO("Servlet defination loaded: "
	         "binaryname= \"%s\", filename=\"%s\", "
	         "desc=\"%s\", size=%zu, version=%x",
	         name, path, def->desc, def->size, def->version);

	/* assign the allocation table */
	addrtab = (runtime_api_address_table_t**)dlsym(dl_handler, RUNTIME_ADDRESS_TABLE_STR);
	if(NULL == addrtab)
	    ERROR_LOG_GOTO(ERR, "Cannot find address table symbol in servlet,"
	                        "make sure symbol `%s' has been defined in the servlet (dlerror: %s)",
	                   RUNTIME_ADDRESS_TABLE_STR, dlerror());

	*addrtab = &runtime_api_address_table;

	ret = (runtime_servlet_binary_t*)malloc(sizeof(runtime_servlet_binary_t));
	if(NULL == ret) ERROR_LOG_GOTO(ERR, "Failed to allocate memory for the servlet binary %s", name);

	ret->define = def;
	ret->dl_handler = dl_handler;

	string_buffer_open(ret->name, sizeof(ret->name), &strbuf);
	string_buffer_append(name, &strbuf);
	string_buffer_close(&strbuf);

#ifdef LOG_NOTICE_ENABLED
	const struct link_map* linkmap = (const struct link_map*)dl_handler;
	LOG_NOTICE("Servlet address map: %s => [%p]", path, (void*)linkmap->l_addr);
#endif


	LOG_DEBUG("Servlet binary %s has been successfully loaded from %s", name, path);

	return ret;

ERR:
	if(dl_handler != NULL) dlclose(dl_handler);
	if(ret != NULL) free(ret);
	return NULL;
}

int runtime_servlet_binary_unload(runtime_servlet_binary_t* binary)
{
	int rc = 0;

	if(NULL == binary)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	/* close the dynamic library */
	if(dlclose(binary->dl_handler) < 0)
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Can not close the dynamic library: %s", dlerror());
	}

	/* free the servlet memory */
	free(binary);

	if(rc != ERROR_CODE(int)) LOG_DEBUG("Servlet has been unloaded successfully");

	return rc;
}

int runtime_servlet_set_trap(runtime_servlet_trap_func_t func)
{
	runtime_api_address_table.trap = func;
	return 0;
}

