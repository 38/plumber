/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <string.h>

#include <error.h>

#include <itc/module_types.h>
#include <itc/binary.h>
#include <module/builtins.h>
#include <lang/prop.h>
#include <utils/log.h>

/**
 * @brief a builtin module binary
 **/
typedef struct {
	const char*          name;    /*!< the name of this binary */
	const itc_module_t*  binary;  /*!< the module binary */
} _builtin_module_binary_t;

static _builtin_module_binary_t _builtins[] = MODULE_BUILTIN_MODULES;

static inline lang_prop_value_t _get_prop(const char* symbol, const void* data)
{
	(void)data;
	lang_prop_value_t ret = {
		.type = LANG_PROP_TYPE_NONE
	};

	if(symbol == NULL)
	    return ret;

	const char* prefix = "has_";
	const char* sp = symbol;

	for(;*prefix && *prefix == *sp; prefix ++, sp ++);

	if(*prefix != 0 || *sp == 0)
	    return ret;

	ret.type = LANG_PROP_TYPE_INTEGER;
	if(itc_binary_search_module(symbol + 4) == NULL)
	    ret.num = 0;
	else
	    ret.num = 1;

	return ret;
}

int itc_binary_init()
{
	lang_prop_callback_t cb = {
		.param = NULL,
		.get   = _get_prop,
		.set   = NULL,
		.symbol_prefix = "module.binary"
	};
	if(ERROR_CODE(int) == lang_prop_register_callback(&cb))
	    ERROR_RETURN_LOG(int, "Cannot register module binary callback");
	return 0;
}

int itc_binary_finalize()
{
	return 0;
}

const itc_module_t* itc_binary_search_module(const char* name)
{
	uint32_t i;
	for(i = 0; i < sizeof(_builtins) / sizeof(_builtins[0]); i ++)
	    if(strcmp(name, _builtins[i].name) == 0) return _builtins[i].binary;
	return NULL;
}


/* TODO: add the external module support at this point */
