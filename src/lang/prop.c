/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <plumber.h>
#include <utils/log.h>
#include <utils/vector.h>
#include <utils/static_assertion.h>
#include <error.h>
/**
 * @brief represents a pair of callback functions
 **/
typedef struct _callback_t{
	const void* param;          /*!< the param for the getter or setter */
	lang_prop_get_func_t get;   /*!< the getter function */
	lang_prop_set_func_t set;   /*!< the setter function */
	struct _callback_t*  next;  /*!< The next callback function in the linked list */
	size_t               sym_prefix_len;  /*!< The length of the symbol prefix */
	uintptr_t            __pading__[0];
	char                 sym_prefix[0];/*!< The symbol prefix the callback handle */
} _callback_t;
STATIC_ASSERTION_LAST(_callback_t, sym_prefix);
STATIC_ASSERTION_SIZE(_callback_t, sym_prefix, 0);

static _callback_t* _callback_list = NULL;

/**
 * @brief create a new callback object in the callback vector
 * @param callback the callback definition that made this callback object
 * @return The newly created callback object
 **/
static inline _callback_t* _callback_obj_new(const lang_prop_callback_t* callback)
{
	size_t len = strlen(callback->symbol_prefix);

	_callback_t* cb = (_callback_t*)malloc(sizeof(_callback_t) + len + 1);

	if(NULL == cb) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the callback function");

	cb->get = callback->get;
	cb->set = callback->set;
	cb->param = callback->param;
	cb->next = NULL;
	cb->sym_prefix_len = len;
	memcpy(cb->sym_prefix, callback->symbol_prefix, len + 1);

	return cb;
}

int lang_prop_init()
{
	return 0;
}

int lang_prop_finalize()
{
	_callback_t* ptr;
	for(ptr = _callback_list; ptr != NULL; )
	{
		_callback_t* this = ptr;
		ptr = ptr->next;
		free(this);
	}
	return 0;
}

int lang_prop_register_callback(const lang_prop_callback_t* callback)
{
	if(NULL == callback || NULL == callback->symbol_prefix) ERROR_RETURN_LOG(int, "Invalid arguments");

	_callback_t* node = _callback_obj_new(callback);
	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot create new callback object");

	node->next = _callback_list;
	_callback_list = node;

	return 0;
}

static inline int _match_callback(const char* target, size_t target_size, const _callback_t* cb)
{
	if(target_size < cb->sym_prefix_len) return 0;
	if(target[cb->sym_prefix_len] != 0 && target[cb->sym_prefix_len] != '.') return 0;
	return memcmp(target, cb->sym_prefix, cb->sym_prefix_len) == 0;
}

static inline const _callback_t* _lookup_callback(const char* symbol)
{
	const _callback_t* ret = NULL;
	const _callback_t* ptr = NULL;
	size_t sym_len = strlen(symbol);
	size_t max_len = 0;
	for(ptr = _callback_list; NULL != ptr; ptr = ptr->next)
	    if(_match_callback(symbol, sym_len, ptr) && max_len < ptr->sym_prefix_len)
	    {
		    ret = ptr;
		    max_len = ret->sym_prefix_len;
	    }
	return ret;
}

lang_prop_value_t lang_prop_get(const char* symbol)
{
	lang_prop_value_t err = {
		.type = LANG_PROP_TYPE_ERROR
	};

	if(NULL == symbol)
	{
		LOG_ERROR("Invalid arguments");
		return err;
	}

	const _callback_t* cb = _lookup_callback(symbol);

	if(NULL == cb || NULL == cb->get)
	{
		lang_prop_value_t none = {
			.type = LANG_PROP_TYPE_NONE
		};
		return none;
	}

	return cb->get(symbol + cb->sym_prefix_len + (symbol[cb->sym_prefix_len] > 0), cb->param);
}

int lang_prop_set(const char* symbol, lang_prop_value_t value)
{
	if(value.type == LANG_PROP_TYPE_ERROR || value.type == LANG_PROP_TYPE_NONE || symbol == NULL)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _callback_t* cb = _lookup_callback(symbol);

	if(NULL == cb || NULL == cb->set) return 0;

	return cb->set(symbol + cb->sym_prefix_len + 1, value, cb->param);
}
