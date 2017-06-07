/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <plumber.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/log.h>
#include <error.h>
#include <module/builtins.h>
#include <utils/thread.h>
itc_module_type_t mod_file;

int list = 0;
const char* servlet_path = NULL;
const char* pipe_redir = NULL;
static struct option _options[] = {
	{"help",    no_argument,        0,  'h'},
	{"list",    no_argument,        0,  'l'},
	{"version", no_argument,        0,  'v'},
	{"path",    required_argument,  0,  's'},
	{"pipe",    required_argument,  0,  'p'},
	{NULL,      0,                  0,   0 }
};

static inline int _load_default_module()
{
	int rc = 0;
	char const * args[3] = {};

	args[0] = "test";
	if(itc_modtab_insmod(&module_test_module_def, 1, args) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_mem_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_file_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_pssm_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	return rc;
}

__attribute__((noreturn)) void display_help(int exitcode)
{

	fprintf(stderr,  "Plumber servlet test tool. The program redirect the pipes to files.\n"
	                 "Usage: pstest [options] servlet.\n");
	fprintf(stderr,  "  -h  --help             Display this usage information.\n"
	                 "  -l  --list             List the pipe defined in this servlet\n"
	                 "  -p  --pipe pipe desc   Redirect the pipe to files. The syntax is PipeName1=File1,PipeName2=File2,PipeName3=File3\n"
	                 "  -s  --path             The servlet search path\n"
	                 "  -v  --version          Show the version of the program\n"
	       );
	exit(exitcode);
}
__attribute__((noreturn)) void show_version(int exitcode)
{
	fprintf(stderr, "Program Version:          %s\n"
	                "Libplumber Version:       %s\n",
	                PLUMBER_VERSION,
	                plumber_version());
	exit(exitcode);
}
void split(const char* string, char delim, void (*action)(char*))
{
	int size;
	static char buffer[4096];

	for(size = 0;*string; string ++)
	{
		if(*string == delim)
		{
			buffer[size] = 0;
			action(buffer);
			size = 0;
		}
		else
		    buffer[size++] = *string;
	}
	if(size > 0)
	{
		buffer[size] = 0;
		action(buffer);
	}
}
void add_search_path(char* path)
{
	runtime_servlet_append_search_path(path);
}

static inline const char* _flagstr(runtime_api_pipe_flags_t flags)
{
	static char buffer[1024];
	string_buffer_t sb;
	string_buffer_open(buffer, sizeof(buffer), &sb);

	if(flags & RUNTIME_API_PIPE_ASYNC)   string_buffer_append("A", &sb);
	if(flags & RUNTIME_API_PIPE_PERSIST) string_buffer_append("P", &sb);
	if(RUNTIME_API_PIPE_IS_INPUT(flags)) string_buffer_append("R", &sb);
	if(flags & RUNTIME_API_PIPE_SHADOW) string_buffer_append("S", &sb);
	if(RUNTIME_API_PIPE_IS_OUTPUT(flags)) string_buffer_append("W", &sb);
	if(RUNTIME_API_PIPE_DISABLED & flags) string_buffer_append("D", &sb);

	if(flags & RUNTIME_API_PIPE_SHADOW) string_buffer_appendf(&sb, "->%x", RUNTIME_API_PIPE_GET_TARGET(flags));

	const char* ret = string_buffer_close(&sb);

	return (ret == NULL || strlen(ret) == 0) ? "?" : ret;
}
void show_pipes(uint32_t argc, char const* const* argv)
{
	runtime_stab_entry_t sid = runtime_stab_load(argc, argv);

	if(ERROR_CODE(runtime_stab_entry_t) == sid)
	{
		LOG_ERROR("cannot find servlet");
		return;
	}

	const runtime_pdt_t* pdt = runtime_stab_get_pdt(sid);

	if(NULL == pdt)
	{
		LOG_ERROR("cannot load the pipe description table");
		return;
	}

	printf("Name    : %s\n", argv[0]);
	printf("Desc    : %s\n", runtime_stab_get_description(sid));
	printf("Version : 0x%.8x\n", runtime_stab_get_version(sid));

	size_t size = runtime_pdt_get_size(pdt);
	if(size == ERROR_CODE(size_t))
	{
		LOG_ERROR("invalid pipe description table size %zu", size);
		return;
	}

	runtime_api_pipe_id_t i;
	size_t name_max = 4, j;
	for(i = 0; i < size; i ++)
	{
		const char* name = runtime_pdt_get_name(pdt, i);
		size_t size = strlen(name) + 3 + strlen(runtime_pdt_type_expr(pdt, i));
		if(name_max < size) name_max = size;
	}

	printf("Pipes   : [ID]\tName");
	for(j = 4; j < name_max; j ++) putchar(' ');
	printf("    Flags\n");

	for(i = 0; i < size; i ++)
	{
		const char* name = runtime_pdt_get_name(pdt, i);
		runtime_api_pipe_flags_t flags = runtime_pdt_get_flags_by_pd(pdt, i);

		printf("          [%2d]\t%s:[%s]", i, name, runtime_pdt_type_expr(pdt, i));
		for(j = strlen(name) + 3 + strlen(runtime_pdt_type_expr(pdt, i)); j < name_max + 4; j ++) putchar(' ');
		printf("0x%.8x(%s)\n", flags, _flagstr(flags));
	}
}

runtime_task_t* task = NULL;
runtime_stab_entry_t sid;
runtime_api_pipe_flags_t forks[65536];
const char* filenames[65536];
void init_pipe(char* cmd)
{
	char* pipe = cmd;
	char* file = cmd;
	for(;*file != '=' && *file; file ++);

	if(*file == 0)
	{
		LOG_FATAL("Invalid pipe redirection");
		exit(1);
	}

	*(file++) = 0;

	runtime_api_pipe_id_t pid = runtime_stab_get_pipe(sid, pipe);
	if(pid == ERROR_CODE(runtime_api_pipe_id_t))
	{
		LOG_FATAL("no such pipe named %s", pipe);
		exit(1);
	}
	runtime_api_pipe_flags_t flags = runtime_stab_get_pipe_flags(sid, pid);
	if(flags == ERROR_CODE(runtime_api_pipe_flags_t))
	{
		LOG_FATAL("can not get the flags of pipe %s", pipe);
		exit(1);
	}


	itc_module_pipe_t *ph;
	itc_module_pipe_t** input = NULL, ** output = NULL;

	if(RUNTIME_API_PIPE_IS_INPUT(flags)) input = &ph;
	else output = &ph;

	itc_module_pipe_param_t param = {
		.input_flags = flags,
		.output_flags = flags,
		.args = file
	};

	forks[pid] = flags;
	filenames[pid] = file;
	if(flags & RUNTIME_API_PIPE_SHADOW)  return;
	else if(itc_module_pipe_allocate(mod_file, 0, param, output, input) < 0)
	{
		LOG_FATAL("Cannot create pipe for input %s", pipe);
		exit(1);
	}

	task->pipes[pid] = ph;
}
void run_task(uint32_t argc, char const* const* argv)
{
	sid = runtime_stab_load(argc, argv);
	if(sid == ERROR_CODE(runtime_stab_entry_t))
	{
		LOG_FATAL("Cannot load servlet");
		exit(1);
	}
	task = runtime_stab_create_exec_task(sid, 0);
	if(NULL != pipe_redir)
	    split(pipe_redir, ',', init_pipe);

	runtime_api_pipe_id_t pid;
	for(pid = 0; pid < task->npipes; pid ++)
	    if(forks[pid] & RUNTIME_API_PIPE_SHADOW)
	    {
		    runtime_api_pipe_id_t target = RUNTIME_API_PIPE_GET_TARGET(forks[pid]);
		    if(task->pipes[target] == NULL) continue;
		    itc_module_pipe_t* pipe = itc_module_pipe_fork(task->pipes[target], forks[pid], 0, filenames[pid]);
		    if(NULL == pipe)
		    {
			    LOG_FATAL("Cannot fork the pipe");
			    exit(1);
		    }
		    /* the pipe handle is actually the input side of the pipe, we put here, only want to make it gets disposed after
		     * the task is deallocated */
		    task->pipes[pid] = pipe;
	    }

	if(runtime_task_start(task) < 0)
	{
		LOG_FATAL("Task terminates with an error code");
		exit(1);
	}

	if(runtime_task_free(task) == ERROR_CODE(int))
	{
		LOG_FATAL("Cannot cleanup the task");
		exit(1);
	}
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
	int opt_idx, c, current = 1;
	for(;(c = getopt_long(argc, argv, "p:s:hlv", _options, &opt_idx)) >= 0;)
	{
		int previous = current;
		switch(c)
		{
			case 's':
			case 'p':
			    current += 2;
			    break;
			default:
			    current ++;
			    break;
		}

		/* we do not want to parse the argument beyond the first invalid token, which means it should be servlet argument */
		if(current != optind)
		{
			current = previous;
			break;
		}

		switch(c)
		{
			case 's':
			    servlet_path = optarg;
			    break;
			case 'p':
			    pipe_redir = optarg;
			    break;
			case 'l':
			    list = 1;
			    break;
			case 'h':
			    display_help(EXIT_SUCCESS);

			case 'v':
			    show_version(EXIT_SUCCESS);

			default:
			    fprintf(stderr, "Invalid argument %c\n", c);
			    display_help(EXIT_FAILURE);
		}
	}

	if(NULL == servlet_path)
	    servlet_path = "";

	if(current == argc)
	{
		fprintf(stderr, "Servlet name is required.\n");
		display_help(EXIT_FAILURE);
	}

	plumber_init();
	LOG_DEBUG("Starting plumber servlet testbed with args:");
	int i;
	for(i = 0; i < argc; i ++)
	{
		LOG_DEBUG("(%2d)\t%s", i, argv[i]);
	}

	if(_load_default_module() == ERROR_CODE(int))
	{
		LOG_FATAL("Cannot initialize the required modules");
		plumber_finalize();
		return 1;
	}

	mod_file = itc_modtab_get_module_type_from_path("pipe.file");
	if(mod_file == ERROR_CODE(itc_module_type_t))
	{
		LOG_FATAL("Cannot get file pipe type");
		plumber_finalize();
		return 1;
	}

	split(servlet_path, ':', add_search_path);
	add_search_path(RUNTIME_SERVLET_DEFAULT_SEARCH_PATH);

	if(list == 1) show_pipes((uint32_t)(argc - current), (char const* const*)argv + current);
	else run_task((uint32_t)(argc - current), (char const* const*)argv + current);
	plumber_finalize();
	return 0;
}
