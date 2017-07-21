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
#include <dirent.h>

#include <constants.h>
#include <utils/string.h>
#include <unistd.h>
#include <utils/thread.h>

#include <cli.h>

#include <module.h>
#include <builtin.h>

#ifdef GPROFTOOLS
#include <gperftools/profiler.h>
#endif

pss_vm_t*     current_vm = NULL;
char const* * module_paths = NULL;
char const* * servlet_dirs = NULL;
uint32_t      max_regs = 65536;
uint32_t      compile_only = 0;
uint32_t      disassemble = 0;
uint32_t      do_not_compile = 0;
uint32_t      build_mod = 0;
uint32_t      debug = 1;
int           log_level = 4;
const char*   compiled_output = NULL;


#define _MESSAGE(fmt, args...) fprintf(stderr, fmt"\n", ##args)
void display_help()
{
	_MESSAGE("PScript: The Plumber Service Script Interpreter");
	_MESSAGE("Usage: pscript [options] service_script_file [arguments-to-script]");
	_MESSAGE("  -h  --help          Show this help information");
	_MESSAGE("  -M  --module-path   Set the module search path");
	_MESSAGE("  -B  --build-mod     Build all the modules under module search path");
	_MESSAGE("  -n  --no-debug-info Do not emit any debug info during compilation");
	_MESSAGE("  -c  --compile       The compile only mode");
	_MESSAGE("  -o  --output        The bytecode output directory");
	_MESSAGE("  -d  --disassemble   Disassemble the given module");
	_MESSAGE("  -S  --servlet-dir   Set the servlet search directory");
	_MESSAGE("  -L  --log-level     Set the log level");
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
		{"build-mod",   no_argument,        0,  'B'},
		{"no-debug-info", no_argument,      0,  'n'},
		{"log-level",   required_argument,  0,  'L'},
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
	for(;(c = getopt_long(argc, argv, "hvNM:S:r:co:dBnL:", _options, &opt_idx)) >= 0;)
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
			case 'B':
			    build_mod = 1;
			    break;
			case 'n':
			    debug = 0;
			    break;
			case 'L':
			    log_level = atoi(optarg);
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

void print_bt(pss_vm_backtrace_t* bt)
{
	if(NULL == bt) return;
	print_bt(bt->next);
	LOG_ERROR("\tfunc: %s, line: %u", bt->func, bt->line);
}

int run_user_script(const char* name, int argc, char** argv)
{
	pss_bytecode_module_t* module = module_from_file(name, !compile_only, 1, (int)debug, compiled_output);

	if(NULL == module)
	{
		LOG_FATAL("Cannot load module %s", name);
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

		pss_value_t argv_obj = make_argv(argc, argv);
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
			print_bt(backtrace);
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

	return rc;
}

int file_filter(const struct dirent* ent)
{
	if(ent->d_name[0] == '.') return 0;
	if(ent->d_type == DT_DIR) return 1;
	size_t len = strlen(ent->d_name);
	return strcmp(ent->d_name + len - 4, ".pss") == 0;
}

int compile_dir(const char* path)
{
	int rc = 0;
	int num_dirent;
	struct dirent** dirents = NULL;

	num_dirent = scandir(path, &dirents, file_filter, alphasort);

	if(num_dirent < 0)
	{
		LOG_WARNING("Cannot scan the directory %s", path);
		return 0;
	}

	char pathbuf[PATH_MAX + 1];

	int i;
	_MESSAGE("PScript: Entering directory %s", path);
	for(i = 0; i < num_dirent; i ++)
	{
		if(snprintf(pathbuf, sizeof(pathbuf), "%s/%s", path, dirents[i]->d_name) > (int)sizeof(pathbuf))
		{
			LOG_WARNING("Discarded extra long dir name %s", pathbuf);
			continue;
		}
		if(dirents[i]->d_type == DT_DIR)
		{
			if(ERROR_CODE(int) == compile_dir(pathbuf))
			    ERROR_LOG_GOTO(ERR, "Cannot compile directory %s", pathbuf);
		}
		else
		{
			_MESSAGE("PScript: Compiling module file %s", dirents[i]->d_name);

			pss_bytecode_module_t* module = module_from_file(pathbuf, 0, 1, (int)debug, NULL);

			if(NULL == module)
			    ERROR_LOG_GOTO(ERR, "Cannot compile module file %s", dirents[i]->d_name);
		}
	}
	goto EXIT;
ERR:
	rc = ERROR_CODE(int);
EXIT:
	_MESSAGE("PScript: Leaving directory %s", path);
	if(dirents != NULL)
	{
		for(i = 0; i < num_dirent; i ++) free(dirents[i]);
		free(dirents);
	}
	return rc;
}

int build_system_module()
{
	uint32_t i;
	for(i = 0; module_paths[i]; i ++)
	    if(strcmp(module_paths[i], ".") && strcmp(module_paths[i], "/"))
	        if(ERROR_CODE(int) == compile_dir(module_paths[i]))
	        {
		        _MESSAGE("Cannot compile module directory %s", module_paths[i]);
		        module_unload_all();
		        plumber_finalize();
		        properly_exit(1);
	        }
	return module_unload_all();
}

void pscript_write_log(int level, const char* file, const char* function, int line, const char* fmt, va_list ap)
{
	if(level <= log_level)
	    log_write_va(level, file, function, line, fmt, ap);
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

	if((build_mod && argc - begin > 0))
	{
		_MESSAGE("Wrong number of script file argument");
		display_help();
		properly_exit(1);
	}

	if(plumber_init() == ERROR_CODE(int))
	{
		LOG_FATAL("Cannot initialize libplumber");
		properly_exit(1);
	}

	if(pss_init() == ERROR_CODE(int) || pss_log_set_write_callback(pscript_write_log) == ERROR_CODE(int))
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

	int rc = 0;
	if(build_mod)
	    rc = build_system_module();
	else if(argc - begin == 0)
	    // interactive cli
	    rc = pss_cli_interactive(debug);
	else
	    rc = run_user_script(argv[begin], argc - begin, argv + begin);


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
