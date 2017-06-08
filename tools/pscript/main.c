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

#ifdef GPROFTOOLS
#include <gperftools/profiler.h>
#endif

char const* * include_dirs = NULL;
char const* * predefined_vars = NULL;
char const* * servlet_dirs = NULL;
char const*   rc_file = PSCRIPT_DEFAULT_RC_FILE;
uint32_t      max_regs = 65536;

#define _MESSAGE(fmt, args...) fprintf(stderr, fmt"\n", ##args)
void display_help()
{
	_MESSAGE("PScript: The Plumber Service Script Interpreter");
	_MESSAGE("Usage: pscript [options] service_script_file");
	_MESSAGE("Note: you can also use \"-\" to make the interpreter read from console");
	_MESSAGE("  -D  --define        Define a variable");
	_MESSAGE("  -h  --help          Show this help information");
	_MESSAGE("  -I  --include-dir   Set the script search directory");
	_MESSAGE("  -S  --servlet-dir   Set the servlet search directory");
	_MESSAGE("  -N  --no-rc-file    Do not run any RC file");
	_MESSAGE("  -R  --max-regs      Set the limit of registers used by the VM");
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
	if(NULL != include_dirs) free(include_dirs);
	if(NULL != servlet_dirs) free(servlet_dirs);
	if(NULL != predefined_vars) free(predefined_vars);

	exit(code);
}

int parse_args(int argc, char** argv)
{
	static struct option _options[] = {
		{"help",        no_argument,        0,  'h'},
		{"version",     no_argument,        0,  'v'},
		{"include-dir", required_argument,  0,  'I'},
		{"define"     , required_argument,  0,  'D'},
		{"servlet-dir", required_argument,  0,  'S'},
		{"rc-file",     required_argument,  0,  'r'},
		{"no-rc-file",  no_argument,        0,  'N'},
		{"max-regs",    required_argument,  0,  'R'},
		{NULL,          0,                  0,   0}
	};

	uint32_t include_count = 0;
	uint32_t define_count = 0;
	uint32_t servlet_count = 0;
	include_dirs = (const char**)calloc(1, sizeof(const char*) * (size_t)argc);
	predefined_vars = (const char**)calloc(1, sizeof(const char*) * (size_t)argc);
	servlet_dirs = (const char**)calloc(1, sizeof(const char*) * (size_t)argc);

	int opt_idx, c;
	for(;(c = getopt_long(argc, argv, "hvNI:D:S:r:R:", _options, &opt_idx)) >= 0;)
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
			case 'I':
			    include_dirs[include_count++] = optarg;
			    break;
			case 'D':
			    predefined_vars[define_count++] = optarg;
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
			case 'R':
			    max_regs = (uint32_t)atoi(optarg);
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

	if(runtime_servlet_append_search_path(".") == ERROR_CODE(int))
	    LOG_WARNING("Cannot add default sevlet search path");

	if(lang_lex_add_script_search_path(".") == ERROR_CODE(int))
	    LOG_WARNING("Cannot add default include search path");

	if(lang_lex_add_script_search_path("/") == ERROR_CODE(int))
	    LOG_WARNING("Cannot add default include search path");

	for(i = 0; include_dirs != NULL && include_dirs[i] != NULL; i ++)
	    if(lang_lex_add_script_search_path(include_dirs[i]) == ERROR_CODE(int))
	        LOG_WARNING("Cannot append include search path to the search path list");

	for(i = 0; servlet_dirs != NULL && servlet_dirs[i] != NULL; i ++)
	    if(runtime_servlet_append_search_path(servlet_dirs[i]) == ERROR_CODE(int))
	        LOG_WARNING("Cannot append servlet search path to servlet search list");

	if(runtime_servlet_append_search_path(RUNTIME_SERVLET_DEFAULT_SEARCH_PATH) == ERROR_CODE(int))
	    LOG_WARNING("Cannot append servlet search path to servlet search list");

	char* builtin_buffer = (char*)malloc(4096);
	string_buffer_t sbuf;
	string_buffer_open(builtin_buffer, 4096, &sbuf);
	if(rc_file != NULL && access(rc_file, R_OK) == F_OK)
	    string_buffer_appendf(&sbuf, "include \"%s\";\n", rc_file);
	for(i = 0; predefined_vars != NULL && predefined_vars[i] != NULL; i ++)
	    string_buffer_appendf(&sbuf, "%s;\n", predefined_vars[i]);
	if(strcmp(argv[begin], "-"))
	    string_buffer_appendf(&sbuf, "include \"%s\";\n", argv[begin]);
	else
	    string_buffer_appendf(&sbuf, "include \"/dev/stdin\";\n");
	string_buffer_close(&sbuf);
	lang_lex_t* lexer = lang_lex_from_buffer(builtin_buffer, (uint32_t)strlen(builtin_buffer));
	if(NULL == lexer)
	{
		LOG_FATAL("Cannot open the input file");
		properly_exit(1);
	}

	lang_bytecode_table_t* bc = lang_bytecode_table_new();
	if(NULL == bc)
	{
		LOG_FATAL("Cannot create bytecode table");
		lang_lex_free(lexer);
		properly_exit(1);
	}

	lang_compiler_options_t options = {
		.reg_limit = max_regs
	};
	lang_compiler_t* compiler = lang_compiler_new(lexer, bc, options);
	if(NULL == compiler)
	{
		LOG_FATAL("Cannot create compiler");
		lang_lex_free(lexer);
		lang_bytecode_table_free(bc);
		properly_exit(1);
	}

	if(lang_compiler_compile(compiler) == ERROR_CODE(int))
	{
		lang_compiler_error_t *ptr;
		for(ptr = lang_compiler_get_error(compiler); NULL != ptr; ptr = ptr->next)
		    fprintf(stderr, "Compiler error at `%s' line %u offset %u: %s\n", ptr->file, ptr->line + 1, ptr->off + 1, ptr->message);
		LOG_FATAL("Cannot compile the script");
		lang_lex_free(lexer);
		lang_bytecode_table_free(bc);
		lang_compiler_free(compiler);
		properly_exit(1);
	}

	lang_vm_t* vm = lang_vm_new(bc);
	if(NULL == vm)
	{
		LOG_FATAL("Cannot create VM");
		lang_lex_free(lexer);
		lang_bytecode_table_free(bc);
		lang_compiler_free(compiler);
		properly_exit(1);
	}

	signal(SIGINT, _stop);

	int rc = lang_vm_exec(vm);
	LOG_INFO("VM terminated with exit code %d", rc);

	lang_vm_free(vm);
	lang_compiler_free(compiler);
	lang_lex_free(lexer);
	lang_bytecode_table_free(bc);

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
