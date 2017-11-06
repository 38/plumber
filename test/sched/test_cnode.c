/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
runtime_stab_entry_t servA, servB;
runtime_api_pipe_id_t Ain, Aout, Aerr;
runtime_api_pipe_id_t Bin1, Bin2, Bout, Berr;

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
int test_linear()
{

	sched_service_buffer_t* serv_buf = NULL;
	sched_service_t* serv = NULL;
	sched_cnode_info_t* info = NULL;

	ASSERT_PTR(serv_buf = sched_service_buffer_new(), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_allow_reuse_servlet(serv_buf), CLEANUP_NOP);

	sched_service_node_id_t nodes[11];
	ASSERT_RETOK(sched_service_node_id_t, nodes[0] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[1] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[2] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[3] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[4] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[5] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[6] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[7] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[8] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[9] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[10] = sched_service_buffer_add_node(serv_buf, servB), goto ERR);

	ASSERT_OK(_pipe(serv_buf, nodes, 0, Aout, 1, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 1, Aout, 2, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 2, Aout, 3, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 3, Aout, 4, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 4, Aout, 5, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 5, Aout, 6, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 6, Aout, 7, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 7, Aout, 8, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 8, Aout, 9, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 8, Aerr, 10, Bin1), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 0, Aerr, 10, Bin2), goto ERR);

	ASSERT_OK(sched_service_buffer_set_input(serv_buf, 0, Ain), goto ERR);
	ASSERT_OK(sched_service_buffer_set_output(serv_buf, 9, Aout), goto ERR);

	serv = sched_service_from_buffer(serv_buf);
	ASSERT_PTR(serv, goto ERR);

	info = sched_cnode_analyze(serv);
	ASSERT_PTR(info, goto ERR);

	ASSERT(info->boundary[nodes[0]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[1]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[2]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[3]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[4]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[5]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[6]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[7]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[8]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[9]] == NULL, goto ERR);

	ASSERT(info->boundary[nodes[0]]->count == 0, goto ERR);
	ASSERT(info->boundary[nodes[1]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[2]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[3]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[4]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[5]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[6]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[7]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[8]]->count == 1, goto ERR);

	ASSERT(info->boundary[nodes[0]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[1]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[2]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[3]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[4]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[5]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[6]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[7]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[8]]->output_cancelled == 1, goto ERR);

	ASSERT(info->boundary[nodes[1]]->dest[0].node_id == nodes[10], goto ERR);
	ASSERT(info->boundary[nodes[2]]->dest[0].node_id == nodes[10], goto ERR);
	ASSERT(info->boundary[nodes[3]]->dest[0].node_id == nodes[10], goto ERR);
	ASSERT(info->boundary[nodes[4]]->dest[0].node_id == nodes[10], goto ERR);
	ASSERT(info->boundary[nodes[5]]->dest[0].node_id == nodes[10], goto ERR);
	ASSERT(info->boundary[nodes[6]]->dest[0].node_id == nodes[10], goto ERR);
	ASSERT(info->boundary[nodes[7]]->dest[0].node_id == nodes[10], goto ERR);
	ASSERT(info->boundary[nodes[8]]->dest[0].node_id == nodes[10], goto ERR);

	ASSERT(info->boundary[nodes[1]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[2]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[3]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[4]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[5]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[6]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[7]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[8]]->dest[0].pipe_desc == Bin1, goto ERR);

	ASSERT_OK(sched_cnode_info_free(info), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_free(serv_buf), CLEANUP_NOP);
	ASSERT_OK(sched_service_free(serv), CLEANUP_NOP);
	return 0;
ERR:
	if(NULL != info) sched_cnode_info_free(info);
	if(NULL != serv_buf) sched_service_buffer_free(serv_buf);
	if(NULL != serv) sched_service_free(serv);
	return ERROR_CODE(int);
}
int test_tree()
{
	sched_service_buffer_t* serv_buf = NULL;
	sched_service_t* serv = NULL;
	sched_cnode_info_t* info = NULL;

	sched_service_node_id_t nodes[10];

	ASSERT_PTR(serv_buf = sched_service_buffer_new(), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_allow_reuse_servlet(serv_buf), CLEANUP_NOP);

	ASSERT_RETOK(sched_service_node_id_t, nodes[0] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[1] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[2] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[3] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[4] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[5] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[6] = sched_service_buffer_add_node(serv_buf, servA), goto ERR);

	ASSERT_RETOK(sched_service_node_id_t, nodes[7] = sched_service_buffer_add_node(serv_buf, servB), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[8] = sched_service_buffer_add_node(serv_buf, servB), goto ERR);
	ASSERT_RETOK(sched_service_node_id_t, nodes[9] = sched_service_buffer_add_node(serv_buf, servB), goto ERR);

	ASSERT_OK(_pipe(serv_buf, nodes, 0, Aout, 1, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 0, Aerr, 2, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 1, Aout, 3, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 1, Aerr, 4, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 2, Aout, 5, Ain), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 2, Aerr, 6, Ain), goto ERR);

	ASSERT_OK(_pipe(serv_buf, nodes, 3, Aout, 7, Bin1), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 4, Aout, 7, Bin2), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 5, Aout, 8, Bin1), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 6, Aout, 8, Bin2), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 7, Bout, 9, Bin1), goto ERR);
	ASSERT_OK(_pipe(serv_buf, nodes, 8, Bout, 9, Bin2), goto ERR);

	ASSERT_OK(sched_service_buffer_set_input(serv_buf, 0, Ain), goto ERR);
	ASSERT_OK(sched_service_buffer_set_output(serv_buf, 9, Bout), goto ERR);

	serv = sched_service_from_buffer(serv_buf);
	ASSERT_PTR(serv, goto ERR);

	info = sched_cnode_analyze(serv);
	ASSERT_PTR(info, goto ERR);

	ASSERT(info->boundary[nodes[0]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[1]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[2]] != NULL, goto ERR);
	ASSERT(info->boundary[nodes[3]] == NULL, goto ERR);
	ASSERT(info->boundary[nodes[4]] == NULL, goto ERR);
	ASSERT(info->boundary[nodes[5]] == NULL, goto ERR);
	ASSERT(info->boundary[nodes[6]] == NULL, goto ERR);
	ASSERT(info->boundary[nodes[7]] == NULL, goto ERR);
	ASSERT(info->boundary[nodes[8]] == NULL, goto ERR);
	ASSERT(info->boundary[nodes[9]] == NULL, goto ERR);

	ASSERT(info->boundary[nodes[0]]->count == 0, goto ERR);
	ASSERT(info->boundary[nodes[1]]->count == 1, goto ERR);
	ASSERT(info->boundary[nodes[2]]->count == 1, goto ERR);

	ASSERT(info->boundary[nodes[0]]->output_cancelled == 1, goto ERR);
	ASSERT(info->boundary[nodes[1]]->output_cancelled == 0, goto ERR);
	ASSERT(info->boundary[nodes[2]]->output_cancelled == 0, goto ERR);

	ASSERT(info->boundary[nodes[1]]->dest[0].node_id == nodes[9], goto ERR);
	ASSERT(info->boundary[nodes[2]]->dest[0].node_id == nodes[9], goto ERR);

	ASSERT(info->boundary[nodes[1]]->dest[0].pipe_desc == Bin1, goto ERR);
	ASSERT(info->boundary[nodes[2]]->dest[0].pipe_desc == Bin2, goto ERR);

	ASSERT_OK(sched_cnode_info_free(info), CLEANUP_NOP);
	ASSERT_OK(sched_service_buffer_free(serv_buf), CLEANUP_NOP);
	ASSERT_OK(sched_service_free(serv), CLEANUP_NOP);
	return 0;
ERR:
	if(NULL != info) sched_cnode_info_free(info);
	if(NULL != serv_buf) sched_service_buffer_free(serv_buf);
	if(NULL != serv) sched_service_free(serv);
	return ERROR_CODE(int);
}

int setup()
{
	const char* argv_A[] = {"serv_helperA", "1"};
	const char* argv_B[] = {"serv_helperB", "1"};

	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);
	expected_memory_leakage();
	ASSERT_RETOK(runtime_stab_entry_t, servA = runtime_stab_load(2, argv_A, NULL), CLEANUP_NOP);
	expected_memory_leakage();
	ASSERT_RETOK(runtime_stab_entry_t, servB = runtime_stab_load(2, argv_B, NULL), CLEANUP_NOP);

	Ain = runtime_stab_get_pipe(servA, "stdin");
	ASSERT_RETOK(runtime_api_pipe_id_t, Ain, CLEANUP_NOP);
	Aout = runtime_stab_get_pipe(servA, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, Aout, CLEANUP_NOP);
	Aerr = runtime_stab_get_pipe(servA, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, Aerr, CLEANUP_NOP);

	Bin1 = runtime_stab_get_pipe(servB, "stdin1");
	ASSERT_RETOK(runtime_api_pipe_id_t, Bin1, CLEANUP_NOP);
	Bin2 = runtime_stab_get_pipe(servB, "stdin2");
	ASSERT_RETOK(runtime_api_pipe_id_t, Bin2, CLEANUP_NOP);
	Bout = runtime_stab_get_pipe(servB, "stdout");
	ASSERT_RETOK(runtime_api_pipe_id_t, Bout, CLEANUP_NOP);
	Berr = runtime_stab_get_pipe(servB, "stderr");
	ASSERT_RETOK(runtime_api_pipe_id_t, Berr, CLEANUP_NOP);
	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_linear),
    TEST_CASE(test_tree)
TEST_LIST_END;
