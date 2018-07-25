/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <constants.h>
#include <error.h>

#include <utils/static_assertion.h>
#include <utils/log.h>
#include <utils/mempool/objpool.h>
#include <utils/thread.h>
#include <utils/string.h>

#include <runtime/api.h>
#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/modtab.h>
#include <itc/binary.h>
#include <lang/prop.h>

/**
 * @brief the size of the header
 **/
static size_t _header_size = 0;

/**
 * @brief the moudle instance references table that is sorted by path
 **/
static itc_modtab_instance_t* _path_index[(itc_module_type_t)-1];

/**
 * @brief the module instance reference table that is sorted by module types
 **/
static itc_modtab_instance_t* _modules[(itc_module_type_t)-1];

/**
 * @brief the module limit
 **/
static itc_module_type_t _limit;

void itc_modtab_set_handle_header_size(size_t size)
{
	if(_header_size == 0)
		_header_size = size;
	else
		LOG_ERROR("Attempt to change the size of handle header!");
}

int itc_modtab_init()
{
	return 0;
}

/**
 * @brief dispose a module instnace
 * @param node the module instnace
 * @return status code
 **/
static inline int _mod_free(itc_modtab_instance_t* node)
{
	int rc = 0;

	if(NULL == node) ERROR_RETURN_LOG(int, "Invaild arguments");

	if(node->module->module_cleanup != NULL && node->module->module_cleanup(node->context) == ERROR_CODE(int))
	{
		LOG_ERROR("Error during invoking the clean up function for module %s", node->path);
		rc = ERROR_CODE(int);
	}

	if(NULL != node->handle_pool && ERROR_CODE(int) == mempool_objpool_free(node->handle_pool))
	{
		LOG_ERROR("Cannot dispose the object memory pool for the module");
		rc = ERROR_CODE(int);
	}

	if(NULL != node->context) free(node->context);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	free((void*)node->path);
#pragma GCC diagnostic pop

	free(node);

	return rc;
}

int itc_modtab_on_exit()
{
	int rc = 0;
	uint32_t i;
	for(i = 0; i < _limit; i++)
		if(_modules[i] != NULL && ERROR_CODE(int) == itc_module_on_exit(_modules[i]->module_id))
			rc = ERROR_CODE(int);

	return rc;
}

int itc_modtab_finalize()
{
	int rc = 0;
	uint32_t i;
	for(i = 0; i < _limit; i ++)
		if(_modules[i] != NULL && _mod_free(_modules[i]) == ERROR_CODE(int))
			rc = ERROR_CODE(int);
	return rc;
}

/**
 * @brief initialize the module and return the module context
 * @param module the module to initialize
 * @param argc the argument count
 * @param argv the argument value
 * @return the newly created context, NULL if error occurs
 **/
static inline void* _init_module(const itc_module_t* module, uint32_t argc, char const* const* argv)
{
	if(NULL == module) ERROR_PTR_RETURN_LOG("Invalid arguments");
	void* ret = NULL;
	if(module->context_size > 0)
	{
		LOG_DEBUG("The module requires a context of %zu bytes", module->context_size);
		ret = malloc(module->context_size);
		if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory");
	}
	else LOG_DEBUG("The module requires no context data, passing NULL as context");

	if(module->module_init(ret, argc, argv) == ERROR_CODE(int))
	{
		free(ret);
		ERROR_PTR_RETURN_LOG("Cannot initialize the module context");
	}

	return ret;
}

static inline itc_modtab_instance_t* _mod_load(const itc_module_t* module, uint32_t argc, char const* const* argv)
{
	if(NULL == module || (argc > 0 && argv == NULL)) ERROR_PTR_RETURN_LOG("Invalid arguments");
	if(NULL == module->module_init) ERROR_PTR_RETURN_LOG("Invalid module definition: initializer is missing");
	if(NULL == module->get_path) ERROR_PTR_RETURN_LOG("Invalid module definition: get_path function is missing");

	/* allocate the node object */
	itc_modtab_instance_t* ret = (itc_modtab_instance_t*)malloc(sizeof(itc_modtab_instance_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for new module instance");
	ret->module = module;

	/* Allocate the path buffer */
	char* path_buffer = (char*)malloc(ITC_MODTAB_MAX_PATH);
	if(NULL == path_buffer)
	{
		free(ret);
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the path buffer");
	}
	ret->path = path_buffer;

	/* initialize the context */
	ret->context = _init_module(module, argc, argv);
	if(NULL == ret->context && module->context_size > 0)
		ERROR_LOG_GOTO(ERR, "Cannot create the context for the module");

	/* Create the memory pool if needed */
	if((module->accept != NULL || module->allocate != NULL))
	{
		if(NULL == (ret->handle_pool = mempool_objpool_new((uint32_t)(_header_size + module->handle_size))))
			ERROR_LOG_GOTO(ERR, "Cannot create the memory pool for the module pipe handle");

		/* For the event loop thread, we actually creates the module pipe handle frequently, but it will be
		 * passed to the dispatcher thread, so we want the allocation unit to be large, and allow more items
		 * cached in the TLP */
		mempool_objpool_tlp_policy_t event_loop_policy = {
			.cache_limit = 1024,
			.alloc_unit  = 128,
		};

		if(ERROR_CODE(int) == mempool_objpool_set_thread_policy(ret->handle_pool, THREAD_TYPE_EVENT, event_loop_policy))
			ERROR_LOG_GOTO(ERR, "Cannot setup the event loop policy for the module pipe handle");

		/* For the AsyncIO thread and worker thread, we just dispose them, but no need for allocating them (This is not true
		 * because the mem_pipe, etc, but it doesn't hand off the memory to other thread). So for this kinds of thread, we
		 * don't want the TLP cache too many object, because we want to make sure the object is recycle to the global pool
		 * so that the event thread will be able to use that */
		mempool_objpool_tlp_policy_t dispose_policy = {
			.cache_limit = 32,
			.alloc_unit  = 32
		};

		if(ERROR_CODE(int) == mempool_objpool_set_thread_policy(ret->handle_pool, THREAD_TYPE_WORKER | THREAD_TYPE_IO, dispose_policy))
			ERROR_LOG_GOTO(ERR, "Cannot setup the IO/Worker policy for the module pipe handle");

	}
	else ret->handle_pool = NULL;

	/* ask for the path of the module instance */
	size_t bufsize = ITC_MODTAB_MAX_PATH;
	char *name_buf = path_buffer;
	if(NULL != module->mod_prefix)
	{
		string_buffer_t sbuf;
		string_buffer_open(path_buffer, bufsize, &sbuf);
		string_buffer_appendf(&sbuf, "%s.", module->mod_prefix);
		size_t len = strlen(string_buffer_close(&sbuf));
		name_buf += len;
		bufsize -= len;
	}

	if(NULL == module->get_path(ret->context, name_buf, bufsize))
		LOG_WARNING("Cannot get the path of this module instance, the module may be inaccessible");

	/* If the module path is a empty string and has a prefix, strip the last dot */
	if(*name_buf == 0 && path_buffer != name_buf) name_buf[-1] = 0;

	LOG_TRACE("Module instance instanticated: %s", ret->path);

	return ret;
ERR:
	free(path_buffer);
	free(ret);
	return NULL;
}

/**
 * @brief the callback function used to pass the property get operation in service script to the module
 * @param symbol the symbol id for each section
 * @param data The additional data, which is the module context
 * @return The result
 **/
static inline lang_prop_value_t _get_module_property(const char* symbol, const void* data)
{
	lang_prop_value_t ret = {
		.type = LANG_PROP_TYPE_ERROR
	};

	if(NULL == data || NULL == symbol)
	{
		LOG_ERROR("Invalid arguments");
		return ret;
	}

	const itc_modtab_instance_t* node = (const itc_modtab_instance_t*)data;

	if(symbol[0])
	{
		if(node->module->get_property == NULL)
		{
			ret.type = LANG_PROP_TYPE_NONE;
			return ret;
		}

		itc_module_property_value_t value = node->module->get_property(node->context, symbol);

		switch(value.type)
		{
			case ITC_MODULE_PROPERTY_TYPE_INT:
				ret.type = LANG_PROP_TYPE_INTEGER;
				ret.num  = value.num;
				return ret;
			case ITC_MODULE_PROPERTY_TYPE_STRING:
				ret.type = LANG_PROP_TYPE_STRING;
				ret.str  = value.str;
				return ret;
			case ITC_MODULE_PROPERTY_TYPE_NONE:
				ret.type = LANG_PROP_TYPE_NONE;
				return ret;
			case ITC_MODULE_PROPERTY_TYPE_ERROR:
				return ret;
		}
	}
	else
	{
		ret.type = LANG_PROP_TYPE_INTEGER;
		ret.num  = 1;
		return ret;
	}

	return ret;
}

/**
 * @brief the callback function used to pass the property set operation in service script to the module
 * @param data The module context
 * @param value The value to set
 * @param symbol The symbol to set
 * @return The number of field has been processed or error code
 **/
static inline int _set_module_property(const char* symbol, lang_prop_value_t value, const void* data)
{
	if(NULL == data || NULL == symbol || LANG_PROP_TYPE_NONE == value.type || LANG_PROP_TYPE_ERROR == value.type)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	const itc_modtab_instance_t* node  = (const itc_modtab_instance_t*)data;

	if(node->module->set_property == NULL) return 0;

	itc_module_property_value_t prop;

	switch(value.type)
	{
		case LANG_PROP_TYPE_INTEGER:
			prop.type = ITC_MODULE_PROPERTY_TYPE_INT;
			prop.num  = value.num;
			break;
		case LANG_PROP_TYPE_STRING:
			prop.type = ITC_MODULE_PROPERTY_TYPE_STRING;
			prop.str = value.str;
			break;
		default:
		    ERROR_RETURN_LOG(int, "Unsupported module property type");
	}

	return node->module->set_property(node->context, symbol, prop);
}

int itc_modtab_insmod(const itc_module_t* module, uint32_t argc, char const* const* argv)
{
	itc_modtab_instance_t* node = _mod_load(module, argc, argv);
	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot instantiate module instance");

	_modules[_limit] = node;

	/* do a insertion sort in the path index */
	itc_module_type_t i;
	for(i = 0; i < _limit && strcmp(_path_index[i]->path, node->path) < 0; i ++);

	if(i < _limit && strcmp(_path_index[i]->path, node->path) == 0)
	{
		ERROR_RETURN_LOG(int, "Insmod: Name conflict %s", node->path);
		_mod_free(node);
	}

	if(_limit > i) memmove(_path_index + i + 1, _path_index + i, (itc_module_type_t)(_limit - i) * sizeof(_path_index[0]));
	_path_index[i] = node;

	/* Register the script callbacks */
	lang_prop_callback_t cb = {
		.param = node,
		.get   = _get_module_property,
		.set   = _set_module_property,
		.symbol_prefix = node->path
	};
	if(ERROR_CODE(int) == lang_prop_register_callback(&cb))
		LOG_WARNING("Cannot register the callback function for module instance %s", node->path);
	else
		LOG_DEBUG("Callback function for module %s has been successfully registered", node->path);

	LOG_INFO("Module %s has been registered as module 0x%x", node->path, _limit);
	node->module_id = _limit ++;

	return 0;
}

/**
 * @brief perform a binary search on the path index table on the len-th char in the path
 * @param len the len-th char
 * @param ch the target char to find
 * @param l the left bound
 * @param r the right bound
 * @note the [l, r) is the valid range. And after this function called the value of
 *       *l and *r will get updated to the range in which all the len-th char in pathes
 *       are ch
 * @return 0 if not found, 1 if valid range returned
 **/
static inline int _bsearch(int len, char ch, int* l, int* r)
{
	/* search for the first ch at len */
	int ll = *l, rr = *r;
	if(ch < _path_index[ll]->path[len]) return 0;
	else if(_path_index[ll]->path[len] < ch)
	{
		for(;rr - ll > 1;)
		{
			int mm = (ll + rr) / 2;
			char mc = _path_index[mm]->path[len];

			if(mc < ch)
				ll = mm;
			else
				rr = mm;
		}

		if(rr == *r || ch != _path_index[rr]->path[len]) return 0;
		*l = rr;
	}

	/* search for the last ch */
	ll = *l, rr = *r;
	for(;rr - ll > 1;)
	{
		int mm = (ll + rr) / 2;
		char mc = _path_index[mm]->path[len];

		if(mc <= ch)
			ll = mm;
		else
			rr = mm;
	}

	*r = rr;
	return 1;
}

const itc_modtab_instance_t* itc_modtab_get_from_path(const char* path)
{
	int l = 0, r = _limit, i;
	for(i = 0; path[i] != 0 && r - l > 1; i ++)
		if(_bsearch(i, path[i], &l, &r) == 0) goto NOT_FOUND;

	if(r - l > 0 && strcmp(path, _path_index[l]->path) == 0)
	{
		LOG_DEBUG("Found module instance %s at 0x%x", path, l);
		return _path_index[l];
	}

NOT_FOUND:
	LOG_INFO("No such module instance: %s", path);
	return NULL;
}


int itc_modtab_open_dir(const char* path, itc_modtab_dir_iter_t* iter)
{
	int l = 0, r = _limit, i;
	for(i = 0; path[i] != 0 && r - l > 1; i ++)
		if(_bsearch(i, path[i], &l, &r) == 0) goto EMPTY;

	for(;r > l && path[i] != 0 && _path_index[l]->path[i] == path[i]; i ++);

	if(path[i] == 0)
	{
		iter->begin = (uint8_t)l;
		iter->end   = (uint8_t)r;
		return 0;
	}

EMPTY:
	/**
	 * Actually we don't want to make this as error, because in some cases, like mod_prefix
	 * If the path is not found, we just want to simple ignored.
	 * The error code should be used when a real error happend
	 **/
	iter->begin = iter->end = 0;
	return 0;
}

const itc_modtab_instance_t* itc_modtab_dir_iter_next(itc_modtab_dir_iter_t* iter)
{
	if(NULL == iter) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(iter->end == iter->begin)
	{
		LOG_DEBUG("End of iterator encountered");
		return NULL;
	}

	int n = iter->begin ++;

	return _path_index[n];
}

const itc_modtab_instance_t* itc_modtab_get_from_module_type(itc_module_type_t type)
{
	if(ERROR_CODE(itc_module_type_t) == type) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return _modules[type];
}

itc_module_type_t itc_modtab_get_module_type_from_path(const char* path)
{
	const itc_modtab_instance_t* inst = itc_modtab_get_from_path(path);

	if(NULL == inst) ERROR_RETURN_LOG(itc_module_type_t, "Cannot get module instnace from path %s", path);

	return inst->module_id;
}

