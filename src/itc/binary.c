/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <string.h>

#include <itc/module_types.h>
#include <itc/binary.h>
#include <module/builtins.h>

/**
 * @brief a builtin module binary
 **/
typedef struct {
	const char*          name;    /*!< the name of this binary */
	const itc_module_t*  binary;  /*!< the module binary */
} _builtin_module_binary_t;

static _builtin_module_binary_t _builtins[] = MODULE_BUILTIN_MODULES;

int itc_binary_init()
{
	return 0;
}

int tic_binary_finalize()
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
