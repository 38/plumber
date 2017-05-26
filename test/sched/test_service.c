/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
runtime_stab_entry_t servA;
runtime_stab_entry_t servB;
sched_service_buffer_t* serv_buf;
int node_data[] = {0,1,2,3,4,5};
int current_node, trap_rc;
sched_service_node_id_t nodes[sizeof(node_data) / sizeof(node_data[0])];

static void trap_func(int trap)
{
	if(trap != node_data[current_node]) trap_rc = -1;
	else trap_rc = 0;
}

static inline int _pipe(sched_service_buffer_t* buf, sched_service_node_id_t* nodes,
                        sched_service_node_id_t src_node, runtime_api_pipe_id_t src_pipe,
                        sched_service_node_id_t dst_node, runtime_api_pipe_id_t dst_pipe)
{
	sched_service_pipe_descriptor_t desc = {
		.source_node_id   = nodes[src_node],
		.source_pipe_desc = src_pipe,
		.destination_node_id = nodes[dst_node],
		.destination_pipe_desc = dst_pipe
	};

	return sched_service_buffer_add_pipe(buf, desc);
}
int service_buffer()
{
	ASSERT_PTR(serv_buf = sched_service_buffer_new(), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_allow_reuse_servlet(serv_buf), CLEANUP_NOP);

	ASSERT_RETOK(sched_service_node_id_t, nodes[0] = sched_service_buffer_add_node(serv_buf, servA), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, nodes[1] = sched_service_buffer_add_node(serv_buf, servA), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, nodes[2] = sched_service_buffer_add_node(serv_buf, servA), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, nodes[3] = sched_service_buffer_add_node(serv_buf, servA), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, nodes[4] = sched_service_buffer_add_node(serv_buf, servA), CLEANUP_NOP);
	ASSERT_RETOK(sched_service_node_id_t, nodes[5] = sched_service_buffer_add_node(serv_buf, servA), CLEANUP_NOP);

	runtime_api_pipe_id_t in = runtime_stab_get_pipe(servA, "stdin");
	ASSERT_RETOK(runtime_api_pipe_id_t, in, CLEANUP_NOP);
	runtime_api_pipe_id_t out = runtime_stab_get_pipe(servA, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, out, CLEANUP_NOP);
	runtime_api_pipe_id_t err = runtime_stab_get_pipe(servA, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, err, CLEANUP_NOP);

	LOG_DEBUG("in = %u, out = %u, err = %u", in, out, err);

	ASSERT_OK(_pipe(serv_buf, nodes, 0, out, 1, in), CLEANUP_NOP);
	ASSERT_OK(_pipe(serv_buf, nodes, 0, err, 2, in), CLEANUP_NOP);
	ASSERT_OK(_pipe(serv_buf, nodes, 1, out, 3, in), CLEANUP_NOP);
	ASSERT_OK(_pipe(serv_buf, nodes, 1, err, 4, in), CLEANUP_NOP);
	ASSERT_OK(_pipe(serv_buf, nodes, 2, err, 5, in), CLEANUP_NOP);

	ASSERT(_pipe(serv_buf, nodes, 0, out, 5, in) < 0, CLEANUP_NOP);
	ASSERT(_pipe(serv_buf, nodes, 1, out, 0, in) < 0, CLEANUP_NOP);
	ASSERT(_pipe(serv_buf, nodes, 5, out, 4, out) < 0, CLEANUP_NOP);
	ASSERT(_pipe(serv_buf, nodes, 0, in, 0, in) < 0, CLEANUP_NOP);
	ASSERT(_pipe(serv_buf, nodes, 0, in, 5, out) < 0, CLEANUP_NOP);

	ASSERT_OK(sched_service_buffer_set_input(serv_buf, 0, in), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_set_output(serv_buf, 5, out), CLEANUP_NOP);

	return 0;
}

static sched_service_t* service;

int build_service()
{
	ASSERT_PTR(service = sched_service_from_buffer(serv_buf), CLEANUP_NOP);

	runtime_task_t* task;

	runtime_servlet_set_trap(trap_func);

#define FS sched_service_free(service)
	for(current_node = 0; current_node < 6; current_node ++)
	{
		ASSERT_PTR(task = sched_service_create_task(service, nodes[current_node]), FS);
		ASSERT_OK(trap_rc, FS; runtime_task_free(task));
		ASSERT_OK(runtime_task_free(task), FS);
	}
#undef FS

	ASSERT_OK(sched_service_free(service), CLEANUP_NOP);
	return 0;
}

int service_validation_invalid_input()
{
	runtime_api_pipe_id_t in = runtime_stab_get_pipe(servA, "stdin");
	ASSERT_RETOK(runtime_api_pipe_id_t, in, CLEANUP_NOP);
	runtime_api_pipe_id_t out = runtime_stab_get_pipe(servA, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, out, CLEANUP_NOP);
	runtime_api_pipe_id_t err = runtime_stab_get_pipe(servA, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, err, CLEANUP_NOP);

	ASSERT_OK(_pipe(serv_buf, nodes, 5, out, 0, in), CLEANUP_NOP);
	ASSERT(NULL == sched_service_from_buffer(serv_buf), CLEANUP_NOP);

	return 0;
}

int service_validation_circular_dep()
{
#define FB sched_service_buffer_free(buffer)
	sched_service_buffer_t* buffer;
	static sched_service_node_id_t nodes[6];

	ASSERT_PTR(buffer = sched_service_buffer_new(), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_allow_reuse_servlet(buffer), CLEANUP_NOP);

	ASSERT_RETOK(sched_service_node_id_t, nodes[0] = sched_service_buffer_add_node(buffer, servA), FB);
	ASSERT_RETOK(sched_service_node_id_t, nodes[1] = sched_service_buffer_add_node(buffer, servB), FB);
	ASSERT_RETOK(sched_service_node_id_t, nodes[2] = sched_service_buffer_add_node(buffer, servA), FB);
	ASSERT_RETOK(sched_service_node_id_t, nodes[3] = sched_service_buffer_add_node(buffer, servA), FB);
	ASSERT_RETOK(sched_service_node_id_t, nodes[4] = sched_service_buffer_add_node(buffer, servA), FB);
	ASSERT_RETOK(sched_service_node_id_t, nodes[5] = sched_service_buffer_add_node(buffer, servA), FB);

	runtime_api_pipe_id_t inA = runtime_stab_get_pipe(servA, "stdin");
	ASSERT_RETOK(runtime_api_pipe_id_t, inA, CLEANUP_NOP);
	runtime_api_pipe_id_t outA = runtime_stab_get_pipe(servA, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, outA, CLEANUP_NOP);
	runtime_api_pipe_id_t errA = runtime_stab_get_pipe(servA, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, errA, CLEANUP_NOP);

	runtime_api_pipe_id_t in1B = runtime_stab_get_pipe(servB, "stdin1");
	ASSERT_RETOK(runtime_api_pipe_id_t, in1B, CLEANUP_NOP);
	runtime_api_pipe_id_t in2B = runtime_stab_get_pipe(servB, "stdin2");
	ASSERT_RETOK(runtime_api_pipe_id_t, in2B, CLEANUP_NOP);
	runtime_api_pipe_id_t outB = runtime_stab_get_pipe(servB, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, outB, CLEANUP_NOP);
	runtime_api_pipe_id_t errB = runtime_stab_get_pipe(servB, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, errB, CLEANUP_NOP);

	ASSERT_OK(_pipe(buffer, nodes, 0, outA, 1, in1B), FB);

	ASSERT_OK(_pipe(buffer, nodes, 1, outB, 2, inA), FB);
	ASSERT_OK(_pipe(buffer, nodes, 2, outA, 3, inA), FB);
	ASSERT_OK(_pipe(buffer, nodes, 3, outA, 4, inA), FB);
	ASSERT_OK(_pipe(buffer, nodes, 4, outA, 5, inA), FB);
	ASSERT_OK(_pipe(buffer, nodes, 4, errA, 1, in2B), FB);

	ASSERT_OK(sched_service_buffer_set_input(buffer, nodes[0], inA), FB);
	ASSERT_OK(sched_service_buffer_set_output(buffer, nodes[5], outA), FB);

	ASSERT(NULL == sched_service_from_buffer(buffer), FB);

	ASSERT_OK(sched_service_buffer_free(buffer), FB);

#undef FB

	return 0;
}

int service_getters()
{
#define FB sched_service_buffer_free(buffer)
	sched_service_buffer_t* buffer;
	static sched_service_node_id_t nodes[2];

	ASSERT_PTR(buffer = sched_service_buffer_new(), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_allow_reuse_servlet(serv_buf), CLEANUP_NOP);

	ASSERT_RETOK(sched_service_node_id_t, nodes[0] = sched_service_buffer_add_node(buffer, servA), FB);
	ASSERT_RETOK(sched_service_node_id_t, nodes[1] = sched_service_buffer_add_node(buffer, servB), FB);

	runtime_api_pipe_id_t inA = runtime_stab_get_pipe(servA, "stdin");
	ASSERT_RETOK(runtime_api_pipe_id_t, inA, CLEANUP_NOP);
	runtime_api_pipe_id_t outA = runtime_stab_get_pipe(servA, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, outA, CLEANUP_NOP);
	runtime_api_pipe_id_t errA = runtime_stab_get_pipe(servA, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, errA, CLEANUP_NOP);

	runtime_api_pipe_id_t in1B = runtime_stab_get_pipe(servB, "stdin1");
	ASSERT_RETOK(runtime_api_pipe_id_t, in1B, CLEANUP_NOP);
	runtime_api_pipe_id_t in2B = runtime_stab_get_pipe(servB, "stdin2");
	ASSERT_RETOK(runtime_api_pipe_id_t, in2B, CLEANUP_NOP);
	runtime_api_pipe_id_t outB = runtime_stab_get_pipe(servB, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, outB, CLEANUP_NOP);
	runtime_api_pipe_id_t errB = runtime_stab_get_pipe(servB, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, errB, CLEANUP_NOP);

	ASSERT_OK(_pipe(buffer, nodes, 0, outA, 1, in1B), FB);
	ASSERT_OK(_pipe(buffer, nodes, 0, errA, 1, in2B), FB);
	ASSERT_OK(sched_service_buffer_set_input(buffer, nodes[0], inA), FB);
	ASSERT_OK(sched_service_buffer_set_output(buffer, nodes[1], outB), FB);

	sched_service_t* serv;
	const sched_service_pipe_descriptor_t* pds;
	uint32_t n;
	ASSERT_PTR(serv = sched_service_from_buffer(buffer), FB);
#define FBS sched_service_free(serv); sched_service_buffer_free(buffer);
	ASSERT_PTR(pds = sched_service_get_incoming_pipes(serv, nodes[0], &n), FBS);
	ASSERT(0 == n, FBS);
	ASSERT_PTR(pds = sched_service_get_incoming_pipes(serv, nodes[1], &n), FBS);
	ASSERT(2 == n, FBS);
	ASSERT(nodes[1] == pds[0].destination_node_id, FBS);
	ASSERT(nodes[1] == pds[1].destination_node_id, FBS);
	ASSERT(pds[0].destination_pipe_desc != pds[1].destination_pipe_desc, FBS);
	ASSERT_PTR(pds = sched_service_get_outgoing_pipes(serv, nodes[0], &n), FBS);
	ASSERT(2 == n, FBS);
	ASSERT(nodes[0] == pds[0].source_node_id, FBS);
	ASSERT(nodes[0] == pds[1].source_node_id, FBS);
	ASSERT(pds[0].source_pipe_desc != pds[1].source_pipe_desc, FBS);
	ASSERT_PTR(pds = sched_service_get_outgoing_pipes(serv, nodes[1], &n), FBS);
	ASSERT(0 == n, FBS);

	ASSERT_OK(sched_service_buffer_free(buffer), FBS);
	ASSERT_OK(sched_service_free(serv), FBS);

#undef FB
#undef FBS

	return 0;
}
int setup()
{
	const char* argv_A[] = {"serv_helperA", "1"};
	const char* argv_B[] = {"serv_helperB", "1"};

	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);
	expected_memory_leakage();
	ASSERT_RETOK(runtime_stab_entry_t, servA = runtime_stab_load(2, argv_A), CLEANUP_NOP);
	expected_memory_leakage();
	ASSERT_RETOK(runtime_stab_entry_t, servB = runtime_stab_load(2, argv_B), CLEANUP_NOP);
	return 0;
}

int teardown()
{
	ASSERT_OK(sched_service_buffer_free(serv_buf), CLEANUP_NOP);
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(service_buffer),
    TEST_CASE(build_service),
    TEST_CASE(service_validation_invalid_input),
    TEST_CASE(service_validation_circular_dep),
    TEST_CASE(service_getters)
TEST_LIST_END;
