/**
 * Copyright (C) 2017-2018, Hao Hou
 **/
/**
 * @brief the file that defines which module should be the built-in module
 * @todo make this file automatically generated at compile time
 **/
#ifndef __PLUMBER_MODULE_BUILTINS__
#define __PLUMBER_MODULE_BUILTINS__

#include <constants.h>

#include <module/tcp/module.h>
#include <module/mem/module.h>
#include <module/test/module.h>
#include <module/legacy_file/module.h>
#include <module/pssm/module.h>
#include <module/tls/module.h>
#include <module/simulate/module.h>
#include <module/legacy_file/module.h>

#if MODULE_TLS_ENABLED
#	define _TLS_MODULE_DEF {"tls_pipe", &module_tls_module_def},
#else
#	define _TLS_MODULE_DEF
#endif

#define MODULE_BUILTIN_MODULES {\
	{"tcp_pipe", &module_tcp_module_def},\
	{"mem_pipe", &module_mem_module_def},\
	{"test_pipe",&module_test_module_def},\
	{"legacy_file_pipe", &module_legacy_file_module_def},\
	_TLS_MODULE_DEF \
	{"pssm",      &module_pssm_module_def},\
	{"simulate",  &module_simulate_module_def},\
	{"legacy_file",  &module_legacy_file_module_def} \
}

#endif /* __PLUMBER_MODULE_BUILTINS__ */
