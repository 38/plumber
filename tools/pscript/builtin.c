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

#include <plumber.h>
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

static pss_value_t _pscript_builtin_insmod(pss_vm_t* vm, uint32_t arg_cnt, pss_value_t* args)
{
	(void)vm;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num = PSS_VM_ERROR_TYPE
	};

	if(arg_cnt < 1) return ret;

	if(args[0].kind != PSS_VALUE_KIND_REF || pss_value_ref_type(args[0]) != PSS_VALUE_REF_TYPE_STRING)
	{
		ret.kind = PSS_VALUE_KIND_ERROR;
		ret.num  = PSS_VM_ERROR_TYPE;
		return ret;
	}

	const char* mod_init_str = pss_value_get_data(args[0]);

	if(NULL == mod_init_str)
	{
	    LOG_ERROR_ERRNO("Cannot get the initialization string");
		ret.kind = PSS_VALUE_KIND_ERROR;
		ret.num  = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	uint32_t arg_cap = 32;
	uint32_t argc = 0;
	char** argv = (char**)malloc(arg_cap * sizeof(char*));
	if(NULL == argv)
	{
	    LOG_ERROR_ERRNO("Cannot create the argument buffer");
		ret.kind = PSS_VALUE_KIND_ERROR;
		ret.num  = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	const char* ptr, *begin;
	const itc_module_t* binary = NULL;
	for(begin = ptr = mod_init_str;; ptr ++)
	{
		if(*ptr == ' ' || *ptr == 0)
		{
			if(ptr - begin > 0)
			{
				if(argc >= arg_cap)
				{
					char** new_argv = (char**)realloc(argv, sizeof(char*) * arg_cap * 2);
					if(new_argv == NULL)
					    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resize the argument buffer");
					argv = new_argv;
					arg_cap = arg_cap * 2;
				}

				argv[argc] = (char*)malloc((size_t)(ptr - begin + 1));
				if(NULL == argv[argc])
				    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allcoate memory for the argument string");

				memcpy(argv[argc], begin, (size_t)(ptr - begin));
				argv[argc][ptr-begin] = 0;
				argc ++;
			}
			begin = ptr + 1;
		}

		if(*ptr == 0) break;
	}


	binary = itc_binary_search_module(argv[0]);
	if(NULL == binary) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find the module binary named %s", argv[0]);

	LOG_DEBUG("Found module binary @%p", binary);

	if(ERROR_CODE(int) == itc_modtab_insmod(binary, argc - 1, (char const* const*) argv + 1))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot instantiate the mdoule binary using param %s", mod_init_str);

	ret.kind = PSS_VALUE_KIND_UNDEF;

	goto CLEANUP;
ERR:
	ret.kind = PSS_VALUE_KIND_ERROR;
	ret.num  = PSS_VM_ERROR_INTERNAL;
CLEANUP:

	if(NULL != argv)
	{
		uint32_t i;
		for(i = 0; i < argc; i ++)
		    free(argv[i]);

		free(argv);
	}

	return ret;
}

static pss_value_t _external_get(const char* name)
{
	lang_prop_value_t value = lang_prop_get(name);
	
	pss_value_t ret;
	switch(value.type)
	{
		case LANG_PROP_TYPE_ERROR:
			ret.kind = PSS_VALUE_KIND_ERROR;
			ret.num  = PSS_VM_ERROR_INTERNAL;
			return ret;
		case LANG_PROP_TYPE_INTEGER:
			ret.kind = PSS_VALUE_KIND_NUM;
			ret.num  = value.num;
			return ret;
		case LANG_PROP_TYPE_STRING:
			ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, value.str);
			return ret;
		case LANG_PROP_TYPE_NONE:
			ret.kind = PSS_VALUE_KIND_UNDEF;
			return ret;
		default:
			ret.kind = PSS_VALUE_KIND_ERROR;
			ret.num  = PSS_VM_ERROR_INTERNAL;
	}

	return ret;
}

static int _external_set(const char* name, pss_value_t data)
{
	lang_prop_value_t val = {};

	switch(data.kind)
	{
		case PSS_VALUE_KIND_NUM:
			val.type = LANG_PROP_TYPE_INTEGER;
			val.num  = data.num;
			return lang_prop_set(name, val);
		case PSS_VALUE_KIND_REF:
			if(pss_value_ref_type(data) == PSS_VALUE_REF_TYPE_STRING)
			{
				val.type = LANG_PROP_TYPE_STRING;
				if(NULL == (val.str  = pss_value_get_data(data)))
					ERROR_RETURN_LOG(int, "Cannot get the string value from the string object");
				return lang_prop_set(name, val);
			}
		default:
			ERROR_RETURN_LOG(int, "Cannot handle the symbol %s", name);
	}
}

int builtin_init(pss_vm_t* vm)
{

	pss_vm_external_global_ops_t ops = {
		.get = _external_get,
		.set = _external_set
	};

	if(ERROR_CODE(int) == pss_vm_set_external_global_callback(vm, ops))
		ERROR_RETURN_LOG(int, "Cannot register the external global accessor");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "print", _pscript_builtin_print))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function print");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "dict", _pscript_builtin_dict))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function print");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "len", _pscript_builtin_len))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function len");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "import", _pscript_builtin_import))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function import");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "insmod", _pscript_builtin_insmod))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function insmod");

	return 0;
}
