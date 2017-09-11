/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <plumber.h>
#include <utils/log.h>
#include <module/builtins.h>
#include <error.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

#include <utils/thread.h>
#include <arch/arch.h>

static int _stopped = 0;
sched_service_t* service;
void _stop(int signo)
{
	(void)signo;
	_stopped = 1;

	LOG_DEBUG("SIGINT Caught!");
	sched_service_free(service);
	plumber_finalize();
	exit(0);
}

static inline int _load_default_module(uint16_t port)
{
	int rc = 0;
	char const * args[3] = {};
	char buf[16];
	snprintf(buf, sizeof(buf), "%u", port);

	args[0] = "test";
	if(itc_modtab_insmod(&module_test_module_def, 1, args) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	args[0] = buf;
	if(itc_modtab_insmod(&module_tcp_module_def, 1, args) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_mem_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_file_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_pssm_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	return rc;
}

int _entry_point(int argc, char** argv)
{
	(void) argc;
	(void) argv;

	const char* reqparse_arg[] = {"reqparse"};
	const char* resgen_arg[]   = {"resgen"};

	plumber_init();
	_load_default_module(8888);

	runtime_servlet_append_search_path(".");
	runtime_stab_entry_t reqparse =  runtime_stab_load(1, reqparse_arg);
	runtime_stab_entry_t resgen =  runtime_stab_load(1, resgen_arg);

	sched_service_buffer_t* buffer = sched_service_buffer_new();

	sched_service_node_id_t reqnode =  sched_service_buffer_add_node(buffer, reqparse);
	sched_service_node_id_t resnode =  sched_service_buffer_add_node(buffer, resgen);
	sched_service_pipe_descriptor_t desc = {
		.source_node_id = reqnode,
		.source_pipe_desc = runtime_stab_get_pipe(reqparse, "user-agent"),
		.destination_node_id = resnode,
		.destination_pipe_desc = runtime_stab_get_pipe(resgen, "user-agent")
	};
	sched_service_buffer_add_pipe(buffer, desc);
	sched_service_buffer_set_input(buffer, reqnode, runtime_stab_get_pipe(reqparse, "request"));
	sched_service_buffer_set_output(buffer, resnode, runtime_stab_get_pipe(resgen, "response"));

	service = sched_service_from_buffer(buffer);
	sched_service_buffer_free(buffer);

	const sched_service_pipe_descriptor_t* sdesc = sched_service_to_pipe_desc(service);

	itc_module_pipe_param_t request_param = {
		.input_flags = sched_service_get_pipe_flags(service, sdesc->source_node_id, sdesc->source_pipe_desc),
		.output_flags = sched_service_get_pipe_flags(service, sdesc->destination_node_id, sdesc->destination_pipe_desc),
		.args = NULL
	};

	signal(SIGINT, _stop);

	/* TODO: dirty hack, make sure we can change this later */
	itc_module_type_t mod_tcp = itc_modtab_get_module_type_from_path("pipe.tcp.port_8888");
	itc_module_type_t mem_pipe = itc_modtab_get_module_type_from_path("pipe.mem");

	sched_task_context_t* stc = sched_task_context_new(NULL);

	for(;!_stopped;)
	{
		itc_module_pipe_t *in, *out;
		itc_module_pipe_accept(mod_tcp, request_param, &in, &out);
		sched_task_new_request(stc, service, in, out);

		while(sched_step_next(stc, mem_pipe) > 0);
	}

	sched_task_context_free(stc);

	return 0;
}

int main(int argc, char** argv)
{
#ifdef STACK_SIZE
	return thread_start_with_aligned_stack(_entry_point, argc, argv);
#else
	return _entry_point(argc, argv);
#endif
}
