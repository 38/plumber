/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <stdio.h>
#include <itc/module_types.h>
#include <module/test/module.h>

runtime_stab_entry_t servletA[10], servletB[10];

sched_service_buffer_t* buffer;

sched_service_node_id_t node[10];

sched_service_t* service;

runtime_api_pipe_id_t A_in, A_out, A_err;
runtime_api_pipe_id_t B_in_1, B_in_2, B_out, B_err;

itc_module_type_t mod_test, mod_mem;

sched_task_context_t* stc = NULL;

int args[] = {1,2,3,4,5,6,7,8,9,10};

int load_servlet()
{
	char argstr[32];
	const char* aa[] = {"serv_helperA", argstr};
	const char* ab[] = {"serv_helperB", argstr};
	expected_memory_leakage();
	int i;
	for(i = 0; i < 10; i ++)
	{
		snprintf(argstr, sizeof(argstr), "%d", args[i]);
		ASSERT_RETOK(runtime_stab_entry_t, servletA[i] = runtime_stab_load(2, aa), CLEANUP_NOP);
		ASSERT_RETOK(runtime_stab_entry_t, servletB[i] = runtime_stab_load(2, ab), CLEANUP_NOP);
		if(i == 0)
		{
			ASSERT_RETOK(runtime_api_pipe_id_t, A_in = runtime_stab_get_pipe(servletA[i], "stdin"), CLEANUP_NOP);
			ASSERT_RETOK(runtime_api_pipe_id_t, A_out = runtime_stab_get_pipe(servletA[i], "stdout"), CLEANUP_NOP);
			ASSERT_RETOK(runtime_api_pipe_id_t, A_err = runtime_stab_get_pipe(servletA[i], "stderr"), CLEANUP_NOP);

			ASSERT_RETOK(runtime_api_pipe_id_t, B_in_1 = runtime_stab_get_pipe(servletB[i], "stdin1"), CLEANUP_NOP);
			ASSERT_RETOK(runtime_api_pipe_id_t, B_in_2 = runtime_stab_get_pipe(servletB[i], "stdin2"), CLEANUP_NOP);
			ASSERT_RETOK(runtime_api_pipe_id_t, B_out = runtime_stab_get_pipe(servletB[i], "stdout"), CLEANUP_NOP);
			ASSERT_RETOK(runtime_api_pipe_id_t, B_err = runtime_stab_get_pipe(servletB[i], "stderr"), CLEANUP_NOP);
		}
		else
		{
			ASSERT(A_in == runtime_stab_get_pipe(servletA[i], "stdin"), CLEANUP_NOP);
			ASSERT(A_out == runtime_stab_get_pipe(servletA[i], "stdout"), CLEANUP_NOP);
			ASSERT(A_err == runtime_stab_get_pipe(servletA[i], "stderr"), CLEANUP_NOP);

			ASSERT(B_in_1 == runtime_stab_get_pipe(servletB[i], "stdin1"), CLEANUP_NOP);
			ASSERT(B_in_2 == runtime_stab_get_pipe(servletB[i], "stdin2"), CLEANUP_NOP);
			ASSERT(B_out == runtime_stab_get_pipe(servletB[i], "stdout"), CLEANUP_NOP);
			ASSERT(B_err == runtime_stab_get_pipe(servletB[i], "stderr"), CLEANUP_NOP);
		}
	}

	return 0;
}
static inline int _pipe(int from_node, runtime_api_pipe_id_t from_pipe, int to_node, runtime_api_pipe_id_t to_pipe)
{
	sched_service_pipe_descriptor_t desc = {
		.source_node_id = node[from_node],
		.source_pipe_desc = from_pipe,
		.destination_node_id = node[to_node],
		.destination_pipe_desc = to_pipe
	};

	return sched_service_buffer_add_pipe(buffer, desc);
}
int single_node_test()
#if DO_NOT_COMPILE_ITC_MODULE_TEST == 0
{
	int rc = -1;
	sched_service_buffer_t* buffer = NULL;
	sched_service_node_id_t node = 0;
	sched_service_t* service = NULL;
	itc_module_pipe_t *input[2] = {}, *output[2] = {};
	int data = 0;
	ASSERT_PTR(buffer = sched_service_buffer_new(), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_allow_reuse_servlet(buffer), CLEANUP_NOP);

	ASSERT_RETOK(sched_service_node_id_t, node = sched_service_buffer_add_node(buffer, servletA[5]), goto ERR);

	ASSERT_OK(sched_service_buffer_set_input(buffer, node, A_in), goto ERR);
	ASSERT_OK(sched_service_buffer_set_output(buffer, node, A_out), goto ERR);

	ASSERT_PTR(service = sched_service_from_buffer(buffer), goto ERR);


	for(data = 0; data < 10; data ++)
	{
		sched_task_t* task;
		int result;
		itc_module_pipe_param_t param = {
			.input_flags = RUNTIME_API_PIPE_INPUT,
			.output_flags = RUNTIME_API_PIPE_OUTPUT,
			.args = NULL
		};
		ASSERT_OK(itc_module_pipe_allocate(mod_test, 0, param, input + 0, input + 1), goto ERR);
		ASSERT_OK(itc_module_pipe_allocate(mod_test, 0, param, output + 0, output + 1), goto ERR);
		ASSERT_RETOK(size_t, itc_module_pipe_write(&data, sizeof(int), input[0]), goto ERR);
		ASSERT_OK(itc_module_pipe_deallocate(input[0]), goto ERR);
		input[0] = NULL;

		ASSERT_RETOK(sched_task_request_t, sched_task_new_request(stc, service, input[1], output[0]), goto ERR);
		input[1] = NULL;
		output[0] = NULL;

		ASSERT_PTR(task = sched_task_next_ready_task(stc), goto TASK_ERR);

		ASSERT_OK(runtime_task_start(task->exec_task), goto TASK_ERR);

		ASSERT_RETOK(size_t, itc_module_pipe_read(&result, sizeof(int), output[1]), goto TASK_ERR);
		ASSERT_OK(itc_module_pipe_deallocate(output[1]), goto TASK_ERR);
		output[1] = NULL;

		ASSERT_OK(sched_task_free(task), goto TASK_ERR);
		task = NULL;

		ASSERT(NULL == sched_task_next_ready_task(stc), goto ERR);
		ASSERT(result == data * 7, goto ERR);
		continue;
TASK_ERR:
		if(NULL != task) sched_task_free(task);
		goto ERR;
	}

	rc = 0;
ERR:
	if(NULL != buffer) sched_service_buffer_free(buffer);
	if(NULL != service) sched_service_free(service);
	if(NULL != input[0]) itc_module_pipe_deallocate(input[0]);
	if(NULL != input[1]) itc_module_pipe_deallocate(input[1]);
	if(NULL != output[0]) itc_module_pipe_deallocate(output[0]);
	if(NULL != output[1]) itc_module_pipe_deallocate(output[1]);
	return rc;
}
#else
{
	LOG_WARNING("Test case disabled because no testing module compiled");
	return 0;
}
#endif /* DO_NOT_COMPILE_ITC_MODULE_TEST */

int build_buffer()
{
	ASSERT_PTR(buffer = sched_service_buffer_new(), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_allow_reuse_servlet(buffer), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[0] = sched_service_buffer_add_node(buffer, servletA[0]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[1] = sched_service_buffer_add_node(buffer, servletA[1]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[2] = sched_service_buffer_add_node(buffer, servletA[2]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[3] = sched_service_buffer_add_node(buffer, servletA[3]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[4] = sched_service_buffer_add_node(buffer, servletA[4]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[5] = sched_service_buffer_add_node(buffer, servletA[5]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[6] = sched_service_buffer_add_node(buffer, servletA[6]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[7] = sched_service_buffer_add_node(buffer, servletB[7]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[8] = sched_service_buffer_add_node(buffer, servletB[8]), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, node[9] = sched_service_buffer_add_node(buffer, servletB[9]), CLEANUP_NOP);

	ASSERT_OK(_pipe(0, A_out, 1, A_in), CLEANUP_NOP);
	ASSERT_OK(_pipe(0, A_err, 2, A_in), CLEANUP_NOP);
	ASSERT_OK(_pipe(1, A_out, 3, A_in), CLEANUP_NOP);
	ASSERT_OK(_pipe(1, A_err, 4, A_in), CLEANUP_NOP);
	ASSERT_OK(_pipe(2, A_out, 5, A_in), CLEANUP_NOP);
	ASSERT_OK(_pipe(2, A_err, 6, A_in), CLEANUP_NOP);
	ASSERT_OK(_pipe(3, A_out, 7, B_in_1), CLEANUP_NOP);
	ASSERT_OK(_pipe(4, A_out, 7, B_in_2), CLEANUP_NOP);
	ASSERT_OK(_pipe(5, A_out, 8, B_in_1), CLEANUP_NOP);
	ASSERT_OK(_pipe(6, A_out, 8, B_in_2), CLEANUP_NOP);
	ASSERT_OK(_pipe(7, B_out, 9, B_in_1), CLEANUP_NOP);
	ASSERT_OK(_pipe(8, B_out, 9, B_in_2), CLEANUP_NOP);

	ASSERT_OK(sched_service_buffer_set_input(buffer, node[0], A_in), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_set_output(buffer, node[9], B_out), CLEANUP_NOP);
	return 0;
}

static inline int request_test(int seed)
#if DO_NOT_COMPILE_ITC_MODULE_TEST == 0
{
	int outval;
	itc_module_pipe_t *sp[2] = {};
	itc_module_pipe_t *out[2] = {};
	sched_task_t* task;

	itc_module_pipe_param_t param = {
		.input_flags = RUNTIME_API_PIPE_INPUT,
		.output_flags = RUNTIME_API_PIPE_OUTPUT,
		.args = NULL
	};

	if(itc_module_pipe_allocate(mod_test, 0, param, sp + 0, sp + 1) < 0) goto ERR;
	if(itc_module_pipe_allocate(mod_test, 0, param, out + 0, out + 1) < 0) goto ERR;

	LOG_DEBUG("write the seed to the input pipe");
	if(itc_module_pipe_write(&seed, sizeof(int), sp[0]) == ERROR_CODE(size_t)) goto ERR;
	if(itc_module_pipe_deallocate(sp[0]) == ERROR_CODE(int)) goto ERR;
	sched_task_request_t reqid;

	if((reqid = sched_task_new_request(stc, service, sp[1], out[0])) == ERROR_CODE(sched_task_request_t)) goto ERR;

	while(NULL != (task = sched_task_next_ready_task(stc)))
	{
		uint32_t size, i;
		const sched_service_pipe_descriptor_t* result;
		itc_module_pipe_t *pipes[2];

		if(NULL == (result = sched_service_get_outgoing_pipes(task->service, task->node, &size))) goto LERR;
		for(i = 0; i < size; i ++)
		{
			if(itc_module_pipe_allocate(mod_test, 0, param, pipes + 0, pipes + 1) == ERROR_CODE(int))
			    goto LERR;
			//task->exec_task->pipes[result[i].source_pipe_desc] = pipes[0];
			if(sched_task_output_pipe(task, result[i].source_pipe_desc, pipes[0]) == ERROR_CODE(int))
			    goto LERR;

			if(sched_task_input_pipe(stc, task->service, task->request, result[i].destination_node_id, result[i].destination_pipe_desc, pipes[1]) < 0)
			    goto LERR;
		}

		if(runtime_task_start(task->exec_task) < 0)
		    goto LERR;

		if(1 != sched_task_request_status(stc, reqid))
		    goto LERR;

		sched_task_free(task);
		continue;
LERR:
		if(task) sched_task_free(task);
		goto ERR;
	}

	itc_module_pipe_read(&outval, sizeof(int), out[1]);

	itc_module_pipe_deallocate(out[1]);

	LOG_NOTICE("Request Result: %d", outval);

	if(outval != 18 * seed)
	{
		LOG_ERROR("Result validalation failed, expected %d, got %d", 18 * seed, outval);
		return -1;
	}

	if(0 != sched_task_request_status(stc, reqid))
	    return -1;

	return 0;
ERR:
	if(NULL != sp[0]) itc_module_pipe_deallocate(sp[0]);
	if(NULL != sp[1]) itc_module_pipe_deallocate(sp[1]);
	if(NULL != out[0]) itc_module_pipe_deallocate(out[0]);
	if(NULL != out[1]) itc_module_pipe_deallocate(out[1]);
	return -1;
}
#else
{
	LOG_WARNING("Skip request test, because testing ITC module is disabled");
	return 0;
}
#endif /*DO_NOT_COMPILE_ITC_MODULE_TEST */

int do_request_test()
{
	int i = 0;
	for(; i < 100; i ++)
	    ASSERT_OK(request_test(i), CLEANUP_NOP);
	return 0;
}
int build_service()
{
	ASSERT_PTR(service = sched_service_from_buffer(buffer), CLEANUP_NOP);

	return 0;
}
static int executed_flags[8];
static void _trap(int n)
{
	executed_flags[n] = 1;
}
int pipe_disable()
{
	memset(executed_flags, 0, sizeof(executed_flags));
	int rc = 0;
	itc_module_pipe_param_t param = {
		.input_flags = RUNTIME_API_PIPE_INPUT,
		.output_flags = RUNTIME_API_PIPE_OUTPUT,
		.args = NULL
	};
	sched_service_t* service = NULL;
	sched_service_buffer_t* buffer = sched_service_buffer_new();
	sched_service_buffer_allow_reuse_servlet(buffer);
	runtime_stab_entry_t servlet[8];
	sched_service_node_id_t node[8];
	int src = 0;
	itc_module_pipe_t *in, *out;

#define _LS(k, name, id, size) \
	do {\
		char ids[2] = #id;\
		char buf[2] = #size;\
		const char* args[] = {"serv_"#name, ids, buf};\
		ASSERT_RETOK(runtime_stab_entry_t, servlet[k] = runtime_stab_load(3, args), goto ERR);\
		ASSERT_RETOK(sched_service_node_id_t, node[k] = sched_service_buffer_add_node(buffer, servlet[k]), goto ERR);\
	}while(0)

	_LS(0, mux, 0, 6);
	_LS(1, tchelper, 1, 1);
	_LS(2, tchelper, 2, 1);
	_LS(3, tchelper, 3, 1);
	_LS(4, tchelper, 4, 1);
	_LS(5, tchelper, 5, 1);
	_LS(6, tchelper, 6, 1);
	_LS(7, cat, 7, 6);
#undef _LS

#define _P(f_node, f_pipe, t_node, t_pipe) do {\
		runtime_api_pipe_id_t fp = runtime_stab_get_pipe(servlet[f_node], #f_pipe);\
		runtime_api_pipe_id_t tp = runtime_stab_get_pipe(servlet[t_node], #t_pipe);\
		sched_service_pipe_descriptor_t pd = {\
			.source_node_id = f_node,\
			.source_pipe_desc = fp,\
			.destination_node_id = t_node,\
			.destination_pipe_desc = tp\
		};\
		ASSERT_OK(sched_service_buffer_add_pipe(buffer, pd), goto ERR);\
	}while(0)

	_P(0, out0, 1, i0);
	_P(0, out1, 2, i0);
	_P(0, out2, 3, i0);
	_P(0, out3, 4, i0);
	_P(0, out4, 5, i0);
	_P(0, out5, 6, i0);
	_P(1, o0, 7, in0);
	_P(2, o0, 7, in1);
	_P(3, o0, 7, in2);
	_P(4, o0, 7, in3);
	_P(5, o0, 7, in4);
	_P(6, o0, 7, in5);
#undef _P

	do{
		runtime_api_pipe_id_t in = runtime_stab_get_pipe(servlet[0], "data");
		runtime_api_pipe_id_t ou = runtime_stab_get_pipe(servlet[7], "out");
		ASSERT_OK(sched_service_buffer_set_input(buffer, 0, in), goto ERR);
		ASSERT_OK(sched_service_buffer_set_output(buffer, 7, ou), goto ERR);
	}while(0);

	ASSERT_PTR(service = sched_service_from_buffer(buffer), goto ERR);

	uint32_t a = 1;

	ASSERT_OK(module_test_set_request(&a, sizeof(uint32_t)), goto ERR);

	itc_module_pipe_accept(mod_test, param, &in, &out);
	sched_task_new_request(stc, service, in, out);

	while((src = sched_step_next(stc, mod_mem)) > 0);

	ASSERT_OK(src, goto ERR);

	ASSERT(1 == *(uint32_t*)module_test_get_response(), CLEANUP_NOP);

	ASSERT(executed_flags[0] == 1, goto ERR);
	ASSERT(executed_flags[1] == 1, goto ERR);
	ASSERT(executed_flags[2] == 0, goto ERR);
	ASSERT(executed_flags[3] == 0, goto ERR);
	ASSERT(executed_flags[4] == 0, goto ERR);
	ASSERT(executed_flags[5] == 0, goto ERR);
	ASSERT(executed_flags[6] == 0, goto ERR);
	ASSERT(executed_flags[7] == 1, goto ERR);

	goto CLEANUP;
ERR:
	rc = ERROR_CODE(int);
CLEANUP:
	if(buffer != NULL) rc |= sched_service_buffer_free(buffer);
	if(service != NULL) rc |= sched_service_free(service);

	return rc;
}

int task_cancel()
#if DO_NOT_COMPILE_ITC_MODULE_TEST == 0
{
	int rc = 0, i;
	itc_module_pipe_param_t param = {
		.input_flags = RUNTIME_API_PIPE_INPUT,
		.output_flags = RUNTIME_API_PIPE_OUTPUT,
		.args = NULL
	};
	sched_service_t* service = NULL;
	sched_service_buffer_t* buffer = sched_service_buffer_new();
	sched_service_buffer_allow_reuse_servlet(buffer);
	runtime_stab_entry_t servlet[8];
	sched_service_node_id_t node[8];
	const int layout[] = {1, 3, 3, 3, 3, 3, 3, 4};
	itc_module_pipe_t *in, *out;
	const char* message = "this is a test message";
	int src = 0;
	ASSERT_PTR(buffer, goto ERR);
	ASSERT_OK(runtime_servlet_set_trap(_trap), goto ERR);
	for(i = 0; i < 8; i ++)
	{
		char ids[2] = {(char)('0' + i), 0};
		char buf[2] = {(char)('0' + layout[i]), 0};
		const char* args[] = {"serv_tchelper", ids, buf};
		ASSERT_RETOK(runtime_stab_entry_t, servlet[i] = runtime_stab_load(3, args), goto ERR);
	}

	for(i = 0; i < 8; i ++)
	{
		ASSERT_RETOK(sched_service_node_id_t, node[i] = sched_service_buffer_add_node(buffer, servlet[i]), goto ERR);
	}
#define _P(f_node, f_pipe, t_node, t_pipe) do {\
		runtime_api_pipe_id_t fp = runtime_stab_get_pipe(servlet[f_node], #f_pipe);\
		runtime_api_pipe_id_t tp = runtime_stab_get_pipe(servlet[t_node], #t_pipe);\
		sched_service_pipe_descriptor_t pd = {\
			.source_node_id = f_node,\
			.source_pipe_desc = fp,\
			.destination_node_id = t_node,\
			.destination_pipe_desc = tp\
		};\
		ASSERT_OK(sched_service_buffer_add_pipe(buffer, pd), goto ERR);\
	}while(0)
	_P(0, o0, 1, i0);
	_P(0, o1, 2, i0);
	_P(1, o0, 3, i0);
	_P(2, o0, 4, i0);
	_P(3, o0, 5, i0);
	_P(4, o0, 6, i0);
	_P(5, o0, 7, i0);
	_P(6, o0, 7, i1);

	do{
		runtime_api_pipe_id_t in = runtime_stab_get_pipe(servlet[0], "i0");
		runtime_api_pipe_id_t ou = runtime_stab_get_pipe(servlet[7], "o0");
		ASSERT_OK(sched_service_buffer_set_input(buffer, 0, in), goto ERR);
		ASSERT_OK(sched_service_buffer_set_output(buffer, 7, ou), goto ERR);
	}while(0);

	ASSERT_PTR(service = sched_service_from_buffer(buffer), goto ERR);

	ASSERT_OK(module_test_set_request(message, strlen(message)), goto ERR);

	itc_module_pipe_accept(mod_test, param, &in, &out);
	sched_task_new_request(stc, service, in, out);

	while((src = sched_step_next(stc, mod_mem)) > 0);

	ASSERT_OK(src, goto ERR);

	ASSERT_STREQ(message, (const char*)module_test_get_response(), goto ERR);

	ASSERT(executed_flags[0] == 1, goto ERR);
	ASSERT(executed_flags[1] == 1, goto ERR);
	ASSERT(executed_flags[3] == 1, goto ERR);
	ASSERT(executed_flags[5] == 1, goto ERR);
	ASSERT(executed_flags[7] == 1, goto ERR);
	ASSERT(executed_flags[2] == 0, goto ERR);
	ASSERT(executed_flags[4] == 0, goto ERR);
	ASSERT(executed_flags[6] == 0, goto ERR);

	goto CLEANUP;
ERR:
	rc = ERROR_CODE(int);
CLEANUP:
	if(buffer != NULL) rc |= sched_service_buffer_free(buffer);
	if(service != NULL) rc |= sched_service_free(service);

	return rc;
}
#else
{
	LOG_WARNING("Test is disabled because the testing ITC module is not compiled");
	return 0;
}
#endif /* DO_NOT_COMPILE_ITC_MODULE_TEST */
int setup()
{
	mod_test = itc_modtab_get_module_type_from_path("pipe.test.test");
	ASSERT(ERROR_CODE(itc_module_type_t) != mod_test, CLEANUP_NOP);
	mod_mem = itc_modtab_get_module_type_from_path("pipe.mem");
	ASSERT(ERROR_CODE(itc_module_type_t) != mod_mem, CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);
	ASSERT_PTR(stc = sched_task_context_new(NULL), CLEANUP_NOP);

	return 0;
}


int teardown()
{
	if(NULL != buffer)
	{
		ASSERT_OK(sched_service_buffer_free(buffer), CLEANUP_NOP);
	}
	if(NULL != service)
	{
		ASSERT_OK(sched_service_free(service), CLEANUP_NOP);
	}
	ASSERT_OK(sched_task_context_free(stc), CLEANUP_NOP);
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(load_servlet),
    TEST_CASE(single_node_test),
    TEST_CASE(build_buffer),
    TEST_CASE(build_service),
    TEST_CASE(do_request_test),
    TEST_CASE(task_cancel),
    TEST_CASE(pipe_disable)
TEST_LIST_END;
