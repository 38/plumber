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
			LOG_ERROR("Type error: Got invalid value");
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
	if(argc < 1) 
	{
		ret.num = PSS_VM_ERROR_ARGUMENT;
		LOG_ERROR("Argument error: len function requires at least one argument");
		return ret;
	}

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
			LOG_ERROR("Type error: len fucntion doesn't support the input type");
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

	if(argc < 1) 
	{
		ret.num = PSS_VM_ERROR_ARGUMENT;
		LOG_ERROR("Argument error: len function requires at least one argument");
		return ret;
	}
	
	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		if(argv[i].kind != PSS_VALUE_KIND_REF || pss_value_ref_type(argv[i]) != PSS_VALUE_REF_TYPE_STRING) return ret;

		const char* name = (const char*)pss_value_get_data(argv[0]);

		if(module_is_loaded(name)) continue;

		pss_bytecode_module_t* module = module_from_file(name, 1, 1, NULL);
		if(NULL == module) 
		{
			LOG_ERROR("Module error: Cannot load the required module named %s", name);
			return ret;
		}

		if(ERROR_CODE(int) == pss_vm_run_module(vm, module, NULL))
		{
			LOG_ERROR("Module error: The module returns with an error code");
			return ret;
		}
	}

	ret.kind = PSS_VALUE_KIND_UNDEF;

	return ret;
}

static pss_value_t _pscript_builtin_insmod(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num = PSS_VM_ERROR_TYPE
	};

	if(argc < 1)
	{
		ret.num =  PSS_VM_ERROR_ARGUMENT;
		LOG_ERROR("Argument error: len function requires at least one argument");
		return ret;
	}

	if(argv[0].kind != PSS_VALUE_KIND_REF || pss_value_ref_type(argv[0]) != PSS_VALUE_REF_TYPE_STRING)
	{
		ret.kind = PSS_VALUE_KIND_ERROR;
		ret.num  = PSS_VM_ERROR_TYPE;
		LOG_ERROR("Type error: String argument expected in the insmod builtin");
		return ret;
	}
	
	uint32_t module_arg_cap = 32, module_argc = 0, i;
	char** module_argv = (char**)malloc(module_arg_cap * sizeof(char*));
	const itc_module_t* binary = NULL;
	if(NULL == module_argv)
	{
		LOG_ERROR_ERRNO("Cannot create the argument buffer");
		ret.kind = PSS_VALUE_KIND_ERROR;
		ret.num  = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	for(i = 0; i < argc; i ++)
	{
		const char* mod_init_str = pss_value_get_data(argv[i]);

		if(NULL == mod_init_str)
		{
			LOG_ERROR_ERRNO("Cannot get the initialization string");
			ret.kind = PSS_VALUE_KIND_ERROR;
			ret.num  = PSS_VM_ERROR_INTERNAL;
			return ret;
		}

		const char* ptr, *begin = mod_init_str;
		for(begin = ptr = mod_init_str;; ptr ++)
		{
			if(*ptr == ' ' || *ptr == 0)
			{
				if(ptr - begin > 0)
				{
					if(module_argc >= module_arg_cap)
					{
						char** new_argv = (char**)realloc(argv, sizeof(char*) * module_arg_cap * 2);
						if(new_argv == NULL)
							ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resize the argument buffer");
						module_argv = new_argv;
						module_arg_cap = module_arg_cap * 2;
					}

					module_argv[module_argc] = (char*)malloc((size_t)(ptr - begin + 1));
					if(NULL == module_argv[module_argc])
						ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allcoate memory for the argument string");

					memcpy(module_argv[module_argc], begin, (size_t)(ptr - begin));
					module_argv[module_argc][ptr-begin] = 0;
					module_argc ++;
				}
				begin = ptr + 1;
			}

			if(*ptr == 0) break;
		}
	}


	binary = itc_binary_search_module(module_argv[0]);
	if(NULL == binary) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find the module binary named %s", module_argv[0]);

	LOG_DEBUG("Found module binary @%p", binary);

	if(ERROR_CODE(int) == itc_modtab_insmod(binary, module_argc - 1, (char const* const*) module_argv + 1))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot instantiate the mdoule binary using param");

	ret.kind = PSS_VALUE_KIND_UNDEF;

	goto CLEANUP;
ERR:
	ret.kind = PSS_VALUE_KIND_ERROR;
	ret.num  = PSS_VM_ERROR_INTERNAL;
CLEANUP:

	if(NULL != module_argv)
	{
		uint32_t i;
		for(i = 0; i < module_argc; i ++)
		    free(module_argv[i]);

		free(module_argv);
	}

	return ret;
}

static int _free_service(void* mem)
{
	return lang_service_free((lang_service_t*)mem);
}

static pss_value_t _pscript_builtin_service_new(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)argc;
	(void)argv;
	(void)vm;
	
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR
	};

	lang_service_t* serv = lang_service_new();
	if(NULL == serv)
	{
		LOG_ERROR("Cannot create new service object");
		ret.num = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	pss_exotic_creation_param_t cp = {
		.magic_num = LANG_SERVICE_TYPE_MAGIC,
		.type_name = "service",
		.dispose   = _free_service, 
		.data      = serv
	};

	ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_EXOTIC, &cp);
	if(ret.kind == PSS_VALUE_KIND_ERROR)
	{
		lang_service_free(serv);
		return ret;
	}

	return ret;
}

static pss_value_t _pscript_builtin_service_node(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_ARGUMENT
	};

	if(argc != 2) 
		return ret;

	if(argv[0].kind != PSS_VALUE_KIND_REF || argv[1].kind != PSS_VALUE_KIND_REF) 
		return ret;

	if(pss_value_ref_type(argv[0]) != PSS_VALUE_REF_TYPE_EXOTIC ||
	   pss_value_ref_type(argv[1]) != PSS_VALUE_REF_TYPE_STRING)
		return ret;

	pss_exotic_t* obj = (pss_exotic_t*)pss_value_get_data(argv[0]);
	lang_service_t* serv = (lang_service_t*)pss_exotic_get_data(obj, LANG_SERVICE_TYPE_MAGIC);
	if(NULL == serv) return ret;

	const char* init_args = (const char*)pss_value_get_data(argv[1]);
	if(NULL == init_args) return ret;
	int64_t rc;
	if(ERROR_CODE(int64_t) == (rc = lang_service_add_node(serv, init_args)))
		ret.num = PSS_VM_ERROR_INTERNAL;
	else
	{
		ret.kind = PSS_VALUE_KIND_NUM;
		ret.num = rc;
	}

	return ret;
}

static pss_value_t _pscript_builtin_service_ports(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;

	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_ARGUMENT
	};

	if(argc != 2) 
		return ret;

	if(argv[0].kind != PSS_VALUE_KIND_REF || argv[1].kind != PSS_VALUE_KIND_NUM)
		return ret;

	if(pss_value_ref_type(argv[0]) != PSS_VALUE_REF_TYPE_EXOTIC)
		return ret;
	
	pss_exotic_t* obj = (pss_exotic_t*)pss_value_get_data(argv[0]);
	lang_service_t* serv = (lang_service_t*)pss_exotic_get_data(obj, LANG_SERVICE_TYPE_MAGIC);
	if(NULL == serv) return ret;

	char** names = lang_service_node_port_names(serv, argv[1].num);

	if(NULL == names)
	{
		ret.num = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	uint32_t i = 0;
	uint32_t j = 0;
	ret.num = PSS_VM_ERROR_INTERNAL;
	ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
	pss_value_t in = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
	pss_value_t out = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
	if(PSS_VALUE_KIND_ERROR == ret.kind) ERROR_LOG_GOTO(ERR, "Cannot create the result dictionary");
	if(PSS_VALUE_KIND_ERROR == in.kind) ERROR_LOG_GOTO(ERR, "Cannot create the input port list");
	if(PSS_VALUE_KIND_ERROR == out.kind) ERROR_LOG_GOTO(ERR, "Cannot create the output port list");
	
	pss_dict_t* ret_dict = (pss_dict_t*)pss_value_get_data(ret);
	if(NULL == ret_dict) ERROR_LOG_GOTO(ERR, "Cannot get the dictionary object from the result value");
	if(ERROR_CODE(int) == pss_dict_set(ret_dict, "input", in))
		ERROR_LOG_GOTO(ERR, "Cannot put the input list to the result dictionary");
	if(ERROR_CODE(int) == pss_dict_set(ret_dict, "output", out))
		ERROR_LOG_GOTO(ERR, "Cannot put the output list to the result ditionary");

	char keybuf[32];
	uint32_t begin = 0;
	for(j = i = 0; j < 2; j ++)
	{
		pss_dict_t* dict = (pss_dict_t*)pss_value_get_data(j ? out : in);
		if(NULL == dict) ERROR_LOG_GOTO(ERR, "Cannot get the dictionary object from the name list");
		for(; names[i] != NULL; i ++)
		{
			pss_value_t val = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, names[i]);
			if(PSS_VALUE_KIND_ERROR == val.kind)
				ERROR_LOG_GOTO(ERR, "Cannot create the name string");
				
			snprintf(keybuf, sizeof(keybuf), "%d", i - begin);
			if(ERROR_CODE(int) != pss_dict_set(dict, keybuf, val)) continue;
			pss_value_decref(val);
			goto ERR;
		}
		begin = ++i;
	}

	free(names);
	return ret;
ERR:
	if(PSS_VALUE_KIND_ERROR != ret.kind) pss_value_decref(ret);
	if(PSS_VALUE_KIND_ERROR != in.kind)  pss_value_decref(in);
	if(PSS_VALUE_KIND_ERROR != out.kind) pss_value_decref(out);

	if(NULL != names)
	{
		for(;names[i] != NULL || j == 0; i ++)
		{
			if(names[i] == NULL) j ++;
			else free(names[i]);
		}
		free(names);
	}

	ret.kind = PSS_VALUE_KIND_ERROR;
	ret.num  = PSS_VM_ERROR_INTERNAL;
	return ret;
}

static pss_value_t _pscript_builtin_service_pipe(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;

	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_ARGUMENT
	};

	if(argc != 5) 
		return ret;

	if(argv[0].kind != PSS_VALUE_KIND_REF || 
	   argv[1].kind != PSS_VALUE_KIND_NUM || argv[2].kind != PSS_VALUE_KIND_REF ||
	   argv[3].kind != PSS_VALUE_KIND_NUM || argv[4].kind != PSS_VALUE_KIND_REF)
		return ret;

	if(pss_value_ref_type(argv[0]) != PSS_VALUE_REF_TYPE_EXOTIC)
		return ret;
	if(pss_value_ref_type(argv[2]) != PSS_VALUE_REF_TYPE_STRING)
		return ret;
	if(pss_value_ref_type(argv[4]) != PSS_VALUE_REF_TYPE_STRING)
		return ret;
	
	pss_exotic_t* obj = (pss_exotic_t*)pss_value_get_data(argv[0]);
	lang_service_t* serv = (lang_service_t*)pss_exotic_get_data(obj, LANG_SERVICE_TYPE_MAGIC);
	if(NULL == serv) return ret;

	const char* src_port = (const char*)pss_value_get_data(argv[2]);
	const char* dst_port = (const char*)pss_value_get_data(argv[4]);
	if(src_port == NULL || dst_port == NULL)
		return ret;

	if(ERROR_CODE(int) == lang_service_add_edge(serv, argv[1].num, src_port, argv[3].num, dst_port))
	{
		ret.num = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	ret.kind = PSS_VALUE_KIND_UNDEF;
	return ret;
}

static pss_value_t _set_input_or_output(uint32_t argc, pss_value_t* argv, int input)
{
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_ARGUMENT
	};

	if(argc != 3) 
		return ret;

	if(argv[0].kind != PSS_VALUE_KIND_REF || 
	   argv[1].kind != PSS_VALUE_KIND_NUM || argv[2].kind != PSS_VALUE_KIND_REF)
		return ret;

	if(pss_value_ref_type(argv[0]) != PSS_VALUE_REF_TYPE_EXOTIC)
		return ret;
	if(pss_value_ref_type(argv[2]) != PSS_VALUE_REF_TYPE_STRING)
		return ret;
	
	pss_exotic_t* obj = (pss_exotic_t*)pss_value_get_data(argv[0]);
	lang_service_t* serv = (lang_service_t*)pss_exotic_get_data(obj, LANG_SERVICE_TYPE_MAGIC);
	if(NULL == serv) return ret;

	const char* port = (const char*)pss_value_get_data(argv[2]);
	if(port == NULL) return ret;

	int rc = 0;
	if(input) 
		rc = lang_service_set_input(serv, argv[1].num, port);
	else
		rc = lang_service_set_output(serv, argv[1].num, port);

	if(ERROR_CODE(int) == rc)
	{
		ret.num = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	ret.kind = PSS_VALUE_KIND_UNDEF;
	return ret;
}

static pss_value_t _pscript_builtin_service_input(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	return _set_input_or_output(argc, argv, 1);
}

static pss_value_t _pscript_builtin_service_output(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	return _set_input_or_output(argc, argv, 0);
}

static pss_value_t _pscript_builtin_typeof(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;

	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_ARGUMENT
	};

	if(argc != 1) return ret;

	const char* result = "Unknown";

	switch(argv[0].kind)
	{
		case PSS_VALUE_KIND_BUILTIN:
			result = "builtin";
			break;
		case PSS_VALUE_KIND_UNDEF:
			result = "undefined";
			break;
		case PSS_VALUE_KIND_NUM:
			result = "number";
			break;
		case PSS_VALUE_KIND_REF:
			switch(pss_value_ref_type(argv[0]))
			{
				case PSS_VALUE_REF_TYPE_STRING:
					result = "string";
					break;
				case PSS_VALUE_REF_TYPE_DICT:
					result = "dict";
					break;
				case PSS_VALUE_REF_TYPE_CLOSURE:
					result = "closure";
					break;
				case PSS_VALUE_REF_TYPE_EXOTIC:
					result = "exotic";
					break;
				default:
					return ret;
			}
			break;
		default:
			return ret;
	}

	char* s = strdup(result);
	if(NULL == s) 
	{
		ret.num = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, s);
	if(ret.kind == PSS_VALUE_KIND_ERROR)
		free(s);

	return ret;
}

static pss_value_t _pscript_builtin_split(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_ARGUMENT
	};

	if(argc != 1 && argc != 2) 
		return ret;

	if( (PSS_VALUE_KIND_REF != argv[0].kind || pss_value_ref_type(argv[0]) != PSS_VALUE_REF_TYPE_STRING) ||
	   ((PSS_VALUE_KIND_REF != argv[1].kind || pss_value_ref_type(argv[1]) != PSS_VALUE_REF_TYPE_STRING) && argc == 2))
		return ret;

	const char* sep = " ";
	const char* str = (const char*)pss_value_get_data(argv[0]);

	if(argc == 2) sep = (const char*)pss_value_get_data(argv[1]);
	if(NULL == sep || NULL == str) 
	{
		ret.num = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	if(sep[0] == 0)
	{
		ret.num = PSS_VM_ERROR_ARGUMENT;
		return ret;
	}

	ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
	if(PSS_VALUE_KIND_ERROR == ret.kind) return ret;
	pss_dict_t* ret_dict = (pss_dict_t*)pss_value_get_data(ret);
	if(NULL == ret_dict) ERROR_LOG_GOTO(ERR, "Cannot get the dictionary object");
	uint32_t cnt = 0;;

	const char* begin, *end, *matched = sep;
	for(begin = end = str;; end ++)
	{
		if(*end)
		{
			if(*matched == *end)
				matched++;
			else
				end -= (matched - sep), matched = sep;
		}

		if(*matched == 0 || *end == 0) 
		{
			size_t len = *end ? (size_t)(end + 1 - begin - (matched - sep)) : (size_t)(end - begin);
			char* ret = (char*)malloc(len + 1);
			if(NULL == ret) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the new string");
			memcpy(ret, begin, len);
			ret[len] = 0;
			pss_value_t val = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, ret);
			if(PSS_VALUE_KIND_ERROR == val.kind)
			{
				free(ret);
				ERROR_LOG_GOTO(ERR, "Cannot create new string object");
			}

			char buf[32];
			snprintf(buf, sizeof(buf), "%u", cnt);

			if(ERROR_CODE(int) == pss_dict_set(ret_dict, buf, val))
			{
				pss_value_decref(val);
				ERROR_LOG_GOTO(ERR, "Cannot put the string to the result list");
			}
			begin = end + 1;
			matched = sep;
			cnt ++;
		}

		if(*end == 0) break;
	}

	return ret;
ERR:
	if(PSS_VALUE_KIND_ERROR == ret.kind) pss_value_decref(ret);
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
			return 0;
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

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "__service_new", _pscript_builtin_service_new))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function __service_new");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "__service_node", _pscript_builtin_service_node))
		ERROR_RETURN_LOG(int, "Cannot register builtin function __service_node");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "__service_node_ports", _pscript_builtin_service_ports))
		ERROR_RETURN_LOG(int, "Cannot register builtin function __service_node_ports");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "__service_pipe", _pscript_builtin_service_pipe))
		ERROR_RETURN_LOG(int, "Cannot register builtin function __service_pipe");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "__service_input", _pscript_builtin_service_input))
		ERROR_RETURN_LOG(int, "Cannot register builtin function __service_input");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "__service_output", _pscript_builtin_service_output))
		ERROR_RETURN_LOG(int, "Cannot register builtin function __service_output");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "typeof", _pscript_builtin_typeof))
		ERROR_RETURN_LOG(int, "Cannot register builtin function typeof");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "split", _pscript_builtin_split))
		ERROR_RETURN_LOG(int, "Cannot register builtin function split");

	return 0;
}
