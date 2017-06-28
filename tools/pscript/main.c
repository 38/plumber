/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <plumber.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/log.h>
#include <error.h>
#include <signal.h>
#include <getopt.h>
#include <constants.h>
#include <utils/string.h>
#include <unistd.h>
#include <utils/thread.h>

#include <pss.h>

#include <module.h>
#include <builtin.h>

#ifdef GPROFTOOLS
#include <gperftools/profiler.h>
#endif

pss_vm_t*     current_vm = NULL;
char const* * module_paths = NULL;
char const* * servlet_dirs = NULL;
char const*   rc_file = PSCRIPT_DEFAULT_RC_FILE;
uint32_t      max_regs = 65536;
uint32_t      compile_only = 0;
uint32_t      disassemble = 0;
uint32_t      do_not_compile = 0;
const char*   compiled_output = NULL;


#define _MESSAGE(fmt, args...) fprintf(stderr, fmt"\n", ##args)
void display_help()
{
	_MESSAGE("PScript: The Plumber Service Script Interpreter");
	_MESSAGE("Usage: pscript [options] service_script_file");
	_MESSAGE("  -h  --help          Show this help information");
	_MESSAGE("  -M  --module-path   Set the module search path");
	_MESSAGE("  -c  --compile       The compile only mode");
	_MESSAGE("  -o  --output        The bytecode output directory");
	_MESSAGE("  -d  --disassemble   Disassemble the given module");
	_MESSAGE("  -S  --servlet-dir   Set the servlet search directory");
	_MESSAGE("  -N  --no-rc-file    Do not run any RC file");
	_MESSAGE("  -r  --rc-file       Run the RC file before the script gets executed");
	_MESSAGE("  -v  --version       Show version information");
}

void display_version()
{
	_MESSAGE("PScript: The Plumber Service Script Interpreter");
	_MESSAGE("Program Version       : " PLUMBER_VERSION);
	_MESSAGE("Libplumber Version    : %s", plumber_version());
}

__attribute__((noreturn)) void properly_exit(int code)
{
	if(NULL != module_paths) free(module_paths);
	if(NULL != servlet_dirs) free(servlet_dirs);

	exit(code);
}

int parse_args(int argc, char** argv)
{
	static struct option _options[] = {
		{"help",        no_argument,        0,  'h'},
		{"module-path", required_argument,  0,  'M'},
		{"compile",     no_argument,        0,  'c'},
		{"output",      required_argument,  0,  'o'},
		{"disassemble", no_argument,        0,  'd'},
		{"servlet-dir", required_argument,  0,  'S'},
		{"no-rc-file",  no_argument,        0,  'N'},
		{"rc-file",     required_argument,  0,  'r'},
		{"version",     no_argument,        0,  'v'},
		{NULL,          0,                  0,   0}
	};

	uint32_t module_count = 2;
	uint32_t servlet_count = 0;
	module_paths = (const char**)calloc(1, sizeof(const char*) * ((size_t)argc + 3));
	servlet_dirs = (const char**)calloc(1, sizeof(const char*) * (size_t)argc);

	module_paths[0] = ".";
	module_paths[1] = "/";

	int opt_idx, c;
	for(;(c = getopt_long(argc, argv, "hvNM:S:r:co:d", _options, &opt_idx)) >= 0;)
	{
		switch(c)
		{
			case 'v':
			    display_version();
			    properly_exit(0);
			    break;
			case 'M':
			    module_paths[module_count++] = optarg;
			    break;
			case 'C':
			    do_not_compile = 1;
			    break;
			case 'c':
			    compile_only = 1;
			    break;
			case 'o':
			    compiled_output = optarg;
			    break;
			case 'd':
			    disassemble = 1;
			    break;
			case 'h':
			    display_help();
			    properly_exit(0);
			    break;
			case 'S':
			    servlet_dirs[servlet_count++] = optarg;
			    break;
			case 'N':
			    rc_file = NULL;
			    break;
			case 'r':
			    rc_file = optarg;
			    break;
			default:
			    display_help();
			    properly_exit(1);
		}
	}
	module_paths[module_count++] = PSCRIPT_GLOBAL_MODULE_PATH;

	return optind;
}

static void _stop(int signo)
{
	(void)signo;

	LOG_DEBUG("SIGINT Caught!");
	sched_loop_kill();
}

pss_value_t make_argv(int argc, char** argv)
{
	pss_value_t ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
	if(ret.kind == PSS_VALUE_KIND_ERROR)
	{
		LOG_ERROR("Cannot create argv value");
		return ret;
	}

	pss_dict_t* dict = (pss_dict_t*)pss_value_get_data(ret);
	if(NULL == dict)
	    ERROR_LOG_GOTO(ERR, "Cannot get the dictionary object from the dictionary value");

	int i;
	for(i = 0; i < argc; i ++)
	{
		char keybuf[32];
		snprintf(keybuf, sizeof(keybuf), "%d", i);

		size_t len = strlen(argv[i]);
		char* buf = (char*)malloc(len + 1);
		if(NULL == buf) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the argument value");

		memcpy(buf, argv[i], len + 1);

		pss_value_t val = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, buf);
		if(val.kind == PSS_VALUE_KIND_ERROR)
		    ERROR_LOG_GOTO(ERR, "Cannot create value for argv[%d]", i);

		if(ERROR_CODE(int) == pss_dict_set(dict, keybuf, val))
		{
			pss_value_decref(val);
			ERROR_LOG_GOTO(ERR, "Cannot insert the new strng to dictionary");
		}
	}

	return ret;
ERR:
	pss_value_decref(ret);
	ret.kind = PSS_VALUE_KIND_ERROR;
	return ret;
}

static void _print_bt(pss_vm_backtrace_t* bt)
{
	if(NULL == bt) return;
	_print_bt(bt->next);
	LOG_ERROR("\tfunc: %s, line: %u", bt->func, bt->line);
}

#ifndef STACK_SIZE
int main(int argc, char** argv)
#else
int _program(int argc, char** argv);

int main(int argc, char** argv)
{
	return thread_start_with_aligned_stack(_program, argc, argv);
}

int _program(int argc, char** argv)
#endif
{
	signal(SIGPIPE, SIG_IGN);

#ifdef GPROFTOOLS
	ProfilerStart("result.prof");
#endif

	int begin = parse_args(argc, argv);
	int i;

	if(argc - begin < 1)
	{
		_MESSAGE("Missing script file name");
		display_help();
		properly_exit(1);
	}

	if(plumber_init() == ERROR_CODE(int))
	{
		LOG_FATAL("Cannot initialize libplumber");
		properly_exit(1);
	}

	if(pss_init() == ERROR_CODE(int) || pss_log_set_write_callback(log_write_va) == ERROR_CODE(int))
	{
		LOG_FATAL("Cannot initialize libpss");
		properly_exit(1);
	}


	if(runtime_servlet_append_search_path(".") == ERROR_CODE(int))
	    LOG_WARNING("Cannot add default sevlet search path");

	for(i = 0; servlet_dirs != NULL && servlet_dirs[i] != NULL; i ++)
	    if(runtime_servlet_append_search_path(servlet_dirs[i]) == ERROR_CODE(int))
	        LOG_WARNING("Cannot append servlet search path to servlet search list");

	if(runtime_servlet_append_search_path(RUNTIME_SERVLET_DEFAULT_SEARCH_PATH) == ERROR_CODE(int))
	    LOG_WARNING("Cannot append servlet search path to servlet search list");

	if(module_set_search_path(module_paths) == ERROR_CODE(int))
	    LOG_WARNING("Cannot set the PSS module search path");

	pss_bytecode_module_t* module = module_from_file(argv[begin], !compile_only, 1, compiled_output);

	if(NULL == module)
	{
		LOG_FATAL("Cannot load module %s", argv[begin]);
		properly_exit(1);
	}

	signal(SIGINT, _stop);

	int rc = 0;

	if(!compile_only && !disassemble)
	{
		current_vm = pss_vm_new();
		if(NULL == current_vm || ERROR_CODE(int) == builtin_init(current_vm))
		{
			if(current_vm != NULL) pss_vm_free(current_vm);
			pss_bytecode_module_free(module);
			LOG_FATAL("Cannot create PSS Virtual Machine");
			properly_exit(1);
		}

		pss_value_t argv_obj = make_argv(argc - begin, argv + begin);
		if(argv_obj.kind == PSS_VALUE_KIND_ERROR)
		{
			pss_vm_free(current_vm);
			pss_bytecode_module_free(module);
			LOG_FATAL("Cannot create argv object");
			properly_exit(1);
		}

		if(ERROR_CODE(int) == pss_vm_set_global(current_vm, "argv", argv_obj))
		{
			pss_vm_free(current_vm);
			pss_bytecode_module_free(module);
			pss_value_decref(argv_obj);
			LOG_FATAL("Cannot inject the argv to the Virtual Machine");
			properly_exit(1);
		}

		rc = pss_vm_run_module(current_vm, module, NULL);
		LOG_INFO("VM terminated with exit code %d", rc);

		if(ERROR_CODE(int) == rc)
		{
			pss_vm_exception_t* exception = pss_vm_last_exception(current_vm);

			LOG_ERROR("PSS VM Exception: %s", exception->message);

			pss_vm_backtrace_t* backtrace = exception->backtrace;

			LOG_ERROR("======Stack backtrace begin ========");
			_print_bt(backtrace);
			LOG_ERROR("======Stack backtrace end   ========");

			pss_vm_exception_free(exception);
		}

		if(ERROR_CODE(int) == pss_vm_free(current_vm))
		    LOG_WARNING("Cannot dipsoes the VM");
	}
	else if(disassemble)
	{
		rc = pss_bytecode_module_logdump(module);
	}

	if(ERROR_CODE(int) == module_unload_all())
	    LOG_WARNING("Cannot dipsose the module");


	if(pss_finalize() == ERROR_CODE(int))
	    LOG_WARNING("Cannot finalize libpss");

	if(plumber_finalize() == ERROR_CODE(int))
	{
		LOG_WARNING("Cannot finalize libplumber");
		properly_exit(1);
	}

#ifdef GPROFTOOLS
	ProfilerStop();
#endif

	properly_exit(rc);
}
