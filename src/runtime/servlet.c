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
#include <fcntl.h>
#include <stdio.h>

#ifdef __LINUX__
#include <link.h>
#endif

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
	char buffer[PATH_MAX + 1];
	static char ret[PATH_MAX + 1];
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
				if(NULL == realpath(buffer, ret))
				    ERROR_PTR_RETURN_LOG_ERRNO("Cannot resolve the absolute path");
				LOG_DEBUG("Found shared object %s", ret);
				return ret;
			}
		}
	}

	return NULL;
}

static inline int _set_prop(const char* symbol, lang_prop_value_t val, const void* data)
{
	(void) data;
	if(NULL == symbol) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(strcmp(symbol, "path") == 0)
	{
		if(val.type != LANG_PROP_TYPE_STRING) ERROR_RETURN_LOG(int, "Type mismatch");
		runtime_servlet_clear_search_path();

		const char* value = val.str;

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
		LOG_WARNING("Undefined property symbol %s", symbol);
		return 0;
	}

	return 1;
}

static inline lang_prop_value_t _get_prop(const char* symbol, const void* data)
{
	(void) data;


	lang_prop_value_t ret = {
		.type = LANG_PROP_TYPE_ERROR
	};

	if(NULL == symbol)
	{
		LOG_ERROR("Invalid arguments");
		return ret;
	}

	if(strcmp(symbol, "path") == 0)
	{
		size_t i, required_size = 0;

		for(i = 0; i < vector_length(_search_paths); i ++)
		{
			const char* path = *VECTOR_GET_CONST(const char *, _search_paths, i);
			if(NULL == path)
			{
				LOG_ERROR("Cannot get the search path");
				return ret;
			}
			/* Because we can have either a : in the middle of the string or the \0 at the end */
			required_size += strlen(path) + 1;
		}

		if(required_size == 0) required_size = 1;

		char* bufmem = (char*)malloc(required_size);
		if(NULL == bufmem)
		{
			LOG_ERROR_ERRNO("Cannot allocate memory for the result");
			return ret;
		}

		string_buffer_t strbuf;
		string_buffer_open(bufmem, required_size, &strbuf);

		int first = 1;

		for(i = 0; i < vector_length(_search_paths); i ++)
		{
			const char* path = *VECTOR_GET_CONST(const char *, _search_paths, i);
			if(NULL == path)
			{
				free(bufmem);
				LOG_ERROR("Cannot get the search path");
				return ret;
			}

			if(first)
			    first = 0;
			else
			    string_buffer_append(":", &strbuf);

			string_buffer_append(path, &strbuf);
		}

		string_buffer_close(&strbuf);

		ret.type = LANG_PROP_TYPE_STRING;
		ret.str = bufmem;

		return ret;
	}

	LOG_WARNING("Undefined property symbol %s", symbol);

	ret.type = LANG_PROP_TYPE_NONE;
	return ret;
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

runtime_servlet_t* runtime_servlet_new(runtime_servlet_binary_t* binary, uint32_t argc, char const* const* argv)
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

		int rc;
		if((rc = runtime_task_start(init_task)) == ERROR_CODE(int))
		    ERROR_LOG_GOTO(ERR, "init task for servlet instance of %s has failed", binary->name);

		if(rc == RUNTIME_API_INIT_RESULT_SYNC) ret->async = 0;
		else if(rc == RUNTIME_API_INIT_RESULT_ASYNC) ret->async = 1;
		else ERROR_LOG_GOTO(ERR, "Invalid init function return vlaue");
	}

	if(runtime_task_free(init_task) == ERROR_CODE(int))
	    LOG_WARNING("cannot dispose executed init task for the servlet instance %s, memory leak possible!", binary->name);
	else
	    init_task = NULL;

	if(ret->async && ret->bin->define->async_setup == NULL)
	    ERROR_LOG_GOTO(ERR, "Invalid servlet, a async servlet without async_setup function defined");

	LOG_INFO("Servlet instance of %s has been created with arguments", binary->name);

	for(i = 0; i < argc; i ++)
	    LOG_INFO("\targv[%u] = \t%s", i, arguments[i]);

	return ret;
ERR:
	if(pdt != NULL) runtime_pdt_free(pdt);
	if(init_task != NULL) runtime_task_free(init_task);

	if(NULL != binary->define->unload)
	{
		runtime_task_t* unload_task = runtime_task_new(ret, RUNTIME_TASK_FLAG_ACTION_UNLOAD);
		if(NULL == unload_task || ERROR_CODE(int) == runtime_task_start(unload_task))
		    LOG_WARNING("Cannot start the unload task");

		if(NULL != unload_task) runtime_task_free(unload_task);
	}

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

runtime_servlet_binary_t* runtime_servlet_binary_load(const char* path, const char* name, int first_run)
{
	void* dl_handler = NULL;
	runtime_api_servlet_def_t* def = NULL;
	runtime_servlet_binary_t* ret = NULL;
	runtime_api_address_table_t** addrtab = NULL;

	if(NULL == path) ERROR_LOG_GOTO(ERR, "Invalid arguments");

	if(first_run)
	{
		LOG_DEBUG("Adding servlet binary to link namespace 0");
		dl_handler = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
		first_run = 0;
	}
	else
	{
		char temp[PATH_MAX];
		char* p;
		snprintf(temp, sizeof(temp), "%s%s.XXXXXX", RUNTIME_SERVLET_NS1_PREFIX, name);
		for(p = temp + strlen(RUNTIME_SERVLET_NS1_PREFIX); *p; p ++)
		    if(*p == '/') *p = '_';
		int fd = -1, sofd = -1;
		if((sofd = open(path, O_RDONLY)) < 0)
		    ERROR_LOG_ERRNO_GOTO(NS1_ERR, "Cannot open the servlet binary file: %s", path);
		if((fd = mkstemp(temp)) < 0)
		    ERROR_LOG_ERRNO_GOTO(NS1_ERR, "Cannot create temp file for the servlet in namespace 1");
		else LOG_DEBUG("Creating copy of namespace 1 servlet at %s", temp);

		char buf[4096];
		for(;;)
		{
			ssize_t rc = read(sofd, buf, sizeof(buf));
			if(rc < 0)
			    ERROR_LOG_ERRNO_GOTO(NS1_ERR, "Cannot read data from the servlet binary");
			if(rc == 0) break;
			while(rc > 0)
			{
				ssize_t wrc = write(fd, buf, (size_t)rc);
				if(wrc == 0)
				    ERROR_LOG_GOTO(NS1_ERR, "Cannot write bytes to the tempfile");
				if(wrc < 0)
				{
					if(errno == EINTR) continue;
					ERROR_LOG_ERRNO_GOTO(NS1_ERR, "Write error");
				}
				rc -= wrc;
			}
		}

		if(close(sofd) < 0)
		    ERROR_LOG_ERRNO_GOTO(NS1_ERR, "Cannnott close the orignal FD");

		if(close(fd) < 0)
		    ERROR_LOG_ERRNO_GOTO(NS1_ERR, "Cannot close the temp file FD");
		else fd = -1;

		dl_handler = dlopen(temp, RTLD_LOCAL | RTLD_LAZY);
		if(unlink(temp) < 0)
		    ERROR_LOG_ERRNO_GOTO(NS1_ERR, "Cannot delete the tempfile");

		goto NS1_SUCCESS;
NS1_ERR:
		if(dl_handler != NULL) dlclose(dl_handler);
		if(fd >= 0) close(fd);
		if(sofd >= 0) close(sofd);
		unlink(temp);
		return NULL;
NS1_SUCCESS:
		(void)0;
	}

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

	/* Here gives us an chance to support multiple version of APIs */
	*addrtab = &runtime_api_address_table;

	ret = (runtime_servlet_binary_t*)calloc(sizeof(runtime_servlet_binary_t), 1);
	if(NULL == ret) ERROR_LOG_ERRNO_GOTO(ERR, "Failed to allocate memory for the servlet binary %s", name);

	ret->define = def;
	ret->dl_handler = dl_handler;

	if(NULL == (ret->name = strdup(name)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot copy the servlet binary name");

	if(NULL == (ret->path = strdup(path)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot copy the servlet binary path");

#if defined(LOG_NOTICE_ENABLED) && defined(__LINUX__)
	const struct link_map* linkmap = (const struct link_map*)dl_handler;
	LOG_NOTICE("Servlet address map: %s => [%p]", path, (void*)linkmap->l_addr);
#endif


	LOG_DEBUG("Servlet binary %s has been successfully loaded from %s", name, path);

	return ret;

ERR:
	if(dl_handler != NULL) dlclose(dl_handler);
	if(ret != NULL)
	{
		if(NULL != ret->name) free(ret->name);
		if(NULL != ret->path) free(ret->path);
		free(ret);
	}
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

	if(binary->async_pool != NULL && mempool_objpool_free(binary->async_pool) == ERROR_CODE(int))
	    LOG_WARNING("Cannot dispose the async buffer memory pool");

	if(binary->name != NULL) free(binary->name);

	if(binary->path != NULL) free(binary->path);

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

