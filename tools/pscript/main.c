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

char const* * module_paths = NULL;
char const* * servlet_dirs = NULL;
char const*   rc_file = PSCRIPT_DEFAULT_RC_FILE;
uint32_t      max_regs = 65536;

#define _MESSAGE(fmt, args...) fprintf(stderr, fmt"\n", ##args)
void display_help()
{
	_MESSAGE("PScript: The Plumber Service Script Interpreter");
	_MESSAGE("Usage: pscript [options] service_script_file");
	_MESSAGE("  -h  --help          Show this help information");
	_MESSAGE("  -M  --module-path   Set the module search path");
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
		{"version",     no_argument,        0,  'v'},
		{"module-path", required_argument,  0,  'M'},
		{"servlet-dir", required_argument,  0,  'S'},
		{"rc-file",     required_argument,  0,  'r'},
		{"no-rc-file",  no_argument,        0,  'N'},
		{NULL,          0,                  0,   0}
	};

	uint32_t module_count = 2;
	uint32_t servlet_count = 0;
	module_paths = (const char**)calloc(1, sizeof(const char*) * ((size_t)argc + 2));
	servlet_dirs = (const char**)calloc(1, sizeof(const char*) * (size_t)argc);

	module_paths[0] = ".";
	module_paths[1] = "/";

	int opt_idx, c;
	for(;(c = getopt_long(argc, argv, "hvNM:S:r:", _options, &opt_idx)) >= 0;)
	{
		switch(c)
		{
			case 'v':
			    display_version();
			    properly_exit(0);
			    break;
			case 'h':
			    display_help();
			    properly_exit(0);
			    break;
			case 'M':
			    module_paths[module_count++] = optarg;
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

	return optind;
}

static void _stop(int signo)
{
	(void)signo;

	LOG_DEBUG("SIGINT Caught!");
	sched_loop_kill();
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

	if(argc - begin != 1)
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

	if(ERROR_CODE(int) == module_set_search_path(module_paths))
		LOG_WARNING("Cannot set the module search path");
	
	pss_bytecode_module_t* module = module_from_file(argv[begin], 1);

	pss_bytecode_module_logdump(module);

	if(NULL == module) 
	{
		LOG_FATAL("Cannot load module %s", argv[begin]);
		properly_exit(1);
	}

	signal(SIGINT, _stop);

	pss_vm_t* vm = pss_vm_new();
	if(NULL == vm || ERROR_CODE(int) == builtin_init(vm)) 
	{
		pss_bytecode_module_free(module);
		LOG_FATAL("Cannot create PSS Virtual Machine");
		properly_exit(1);
	}

	int rc = pss_vm_run_module(vm, module, NULL);
	LOG_INFO("VM terminated with exit code %d", rc);

	if(ERROR_CODE(int) == rc)
	{
		pss_vm_exception_t* exception = pss_vm_last_exception(vm);

		LOG_ERROR("PSS VM Exception: %s", exception->message);

		pss_vm_exception_free(exception);
	}
	
	if(ERROR_CODE(int) == pss_bytecode_module_free(module))
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
