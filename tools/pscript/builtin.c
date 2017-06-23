/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#include <constants.h>
#include <error.h>

#include <pss.h>
#include <module.h>

extern char const* const* module_paths;

static pss_value_t _pscript_builtin_print(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {};
	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		char buf[4096];
		if(ERROR_CODE(size_t) == pss_value_strify_to_buf(argv[i], buf, sizeof(buf)))
		{
			ret.kind = PSS_VALUE_KIND_ERROR;
			break;
		}

		printf("%s", buf);
	}

	putchar('\n');

	return ret;
}

static pss_value_t _pscript_builtin_dict(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	(void)argc;
	(void)argv;
	pss_value_t ret = {.kind = PSS_VALUE_KIND_ERROR};

	ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);

	return ret;
}

static pss_value_t _pscript_builtin_len(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_TYPE
	};
	if(argc < 1) return ret;

	if(argv[0].kind != PSS_VALUE_KIND_REF) return ret;

	switch(pss_value_ref_type(argv[0]))
	{
		case PSS_VALUE_REF_TYPE_DICT:
		{
			pss_dict_t* dict = (pss_dict_t*)pss_value_get_data(argv[0]);
			if(NULL == dict) break;
			uint32_t result = pss_dict_size(dict);
			if(ERROR_CODE(uint32_t) == result) break;
			ret.kind = PSS_VALUE_KIND_NUM;
			ret.num = result;
			break;
		}
		case PSS_VALUE_REF_TYPE_STRING:
		{
			const char* str = (const char*)pss_value_get_data(argv[0]);
			if(NULL == str) break;
			ret.kind = PSS_VALUE_KIND_NUM;
			ret.num = (int64_t)strlen(str);
			break;
		}
		default:
			break;
	}
	return ret;
}

static pss_value_t _pscript_builtin_import(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num = PSS_VM_ERROR_TYPE
	};

	if(argc < 1) return ret;
	
	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		if(argv[i].kind != PSS_VALUE_KIND_REF || pss_value_ref_type(argv[i]) != PSS_VALUE_REF_TYPE_STRING) return ret;

		const char* name = (const char*)pss_value_get_data(argv[0]);

		if(module_is_loaded(name)) continue;

		pss_bytecode_module_t* module = module_from_file(name, 1, 1, NULL);
		if(NULL == module) return ret;

		if(ERROR_CODE(int) == pss_vm_run_module(vm, module, NULL))
			return ret;
	}

	ret.kind = PSS_VALUE_KIND_UNDEF;

	return ret;
}

int builtin_init(pss_vm_t* vm)
{
	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "print", _pscript_builtin_print))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function print");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "dict", _pscript_builtin_dict))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function print");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "len", _pscript_builtin_len))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function len");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "import", _pscript_builtin_import))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function import");

	return 0;
}
