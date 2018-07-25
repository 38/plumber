/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <stdio.h>
#include <stdarg.h>
unsigned pipe_open_n;
int* pipe_open_rc;
const runtime_pdt_t* pdt;
int pipe_open_dup_rc;
runtime_stab_entry_t sid;
int read_write_test_rc = -1;
int read_inpalce_rc = -1;
int eof_rc = 0;
int cntl_rc = -1;
int cntl_module_rc = -1;
static void trap(int id)
{
	runtime_task_t* task = runtime_task_current();
	const void* data = task->servlet->data;
	const int* intarr = (const int*)data;
	const char* const* strarr = (const char* const*)data;
	switch(id)
	{
		case 0:
			if(intarr[0] < 0) return;
			pdt = task->servlet->pdt;
			pipe_open_n = (unsigned)intarr[0];
			pipe_open_rc = malloc(sizeof(int) * (unsigned)intarr[0]);
			if(pipe_open_rc != NULL) memcpy(pipe_open_rc, intarr + 1, sizeof(int) * (unsigned)intarr[0]);
			return;
		case 1:
			pipe_open_dup_rc = intarr[0];
			return;
		case 2:
			if(strcmp(strarr[0], strarr[1]) == 0)
				read_write_test_rc = 0;
			else
				read_write_test_rc = -1;
			return;
		case 3:
			if(strcmp(strarr[0], strarr[1]) == 0)
				read_inpalce_rc = 0;
			else
				read_inpalce_rc = 1;
			return;
		case 4:
			if(intarr[0] != 0) eof_rc = -1;
			if(intarr[1] != 0) eof_rc = -1;
			break;
		case 5:
			cntl_rc = 0;
			break;
		case 35:
			cntl_module_rc ++;
			break;
		case 105:
			cntl_module_rc ++;
			break;
		default:
		    LOG_WARNING("unknown trap");
	}
}

int test_pipe_open(void)
{
	unsigned i;

	pipe_open_rc = NULL;

	char const* argv[] = {"serv_api_test"};

	ASSERT_RETOK(runtime_stab_entry_t, sid = runtime_stab_load(1, argv, NULL), CLEANUP_NOP);
	expected_memory_leakage();

	ASSERT(pipe_open_n > 0, CLEANUP_NOP);
	ASSERT_PTR(pipe_open_rc, CLEANUP_NOP);
	ASSERT_PTR(pdt, CLEANUP_NOP);

	for(i = 0; i < pipe_open_n; i ++)
		ASSERT((unsigned)pipe_open_rc[i] == i + 2, CLEANUP_NOP);

	for(i = 0; i < pipe_open_n; i ++)
		ASSERT(runtime_pdt_get_flags_by_pd(pdt, (runtime_api_pipe_id_t)(i + 2)) == (runtime_api_pipe_flags_t)(i * 3), CLEANUP_NOP);

	for(i = 0; i < pipe_open_n; i ++)
	{
		static char buf[128];
		snprintf(buf, sizeof(buf), "test%d", i);
		ASSERT(runtime_pdt_get_pd_by_name(pdt, buf) == (runtime_api_pipe_id_t)(i + 2), CLEANUP_NOP);
	}

	ASSERT(pipe_open_dup_rc == ERROR_CODE(runtime_api_pipe_id_t), CLEANUP_NOP);
	return 0;
}

static inline int _pipe_cntl(itc_module_pipe_t* handle, uint32_t opcode, ...)
{
	va_list ap;
	va_start(ap, opcode);
	int rc = itc_module_pipe_cntl(handle, opcode, ap);
	va_end(ap);
	return rc;
}

#if DO_NOT_COMPILE_ITC_MODULE_TEST == 0
int test_pipe_read(void)
{
	itc_module_type_t mod_test = itc_modtab_get_module_type_from_path("pipe.test.test");
	ASSERT(ERROR_CODE(itc_module_type_t) != mod_test, CLEANUP_NOP);

	int ret = ERROR_CODE(int);

	itc_module_pipe_param_t param = {
		.input_flags = RUNTIME_API_PIPE_INPUT,
		.output_flags = RUNTIME_API_PIPE_OUTPUT,
		.args = NULL
	};
	runtime_task_t* task = runtime_stab_create_exec_task(sid, RUNTIME_TASK_FLAG_ACTION_EXEC);
	ASSERT_PTR(task, CLEANUP_NOP);

	ASSERT(task->npipes == pipe_open_n + 2, runtime_task_free(task));

	itc_module_pipe_t* input_side = NULL;

	ASSERT_OK(itc_module_pipe_allocate(mod_test, 0, param, task->pipes, task->pipes + 1), goto ERR);
	ASSERT_OK(itc_module_pipe_allocate(mod_test, 0, param, task->pipes + 2, task->pipes + 3), goto ERR);
	ASSERT_OK(itc_module_pipe_allocate(mod_test, 0, param, task->pipes + 4, task->pipes + 5), goto ERR);
	ASSERT_OK(itc_module_pipe_allocate(mod_test, 0, param, task->pipes + 6, &input_side), goto ERR);

	ASSERT_OK(itc_module_pipe_deallocate(task->pipes[4]), goto ERR);
	task->pipes[4] = NULL;

	ASSERT_OK(runtime_task_start(task), goto ERR);


	ASSERT_OK(read_write_test_rc, goto ERR);
	ASSERT_OK(read_inpalce_rc, goto ERR);
	ASSERT_OK(eof_rc, goto ERR);
	ASSERT_OK(cntl_rc, goto ERR);
	ASSERT(1 == cntl_module_rc, goto ERR);

	runtime_api_pipe_flags_t pf;

	ASSERT_OK(_pipe_cntl(input_side, RUNTIME_API_PIPE_CNTL_OPCODE_GET_FLAGS, &pf), goto ERR);
	ASSERT(pf == RUNTIME_API_PIPE_INPUT, goto ERR);
	ASSERT_OK(runtime_task_free(task), goto ERR);
	task = NULL;

	ASSERT_OK(_pipe_cntl(input_side, RUNTIME_API_PIPE_CNTL_OPCODE_GET_FLAGS, &pf), goto ERR);
	ASSERT(pf == (RUNTIME_API_PIPE_INPUT | RUNTIME_API_PIPE_PERSIST), goto ERR);

	ret = 0;
ERR:
	if(NULL != input_side) itc_module_pipe_deallocate(input_side);
	if(NULL != task) ASSERT_OK(runtime_task_free(task), CLEANUP_NOP);
	return ret;
}
#endif /* DO_NOT_COMPILE_ITC_MODULE_TEST */

int setup(void)
{
	ASSERT_OK(runtime_servlet_append_search_path(TESTDIR), CLEANUP_NOP);
	ASSERT_OK(runtime_servlet_set_trap(trap), CLEANUP_NOP);
	return 0;
}

int teardown(void)
{
	if(pipe_open_rc) free(pipe_open_rc);
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_pipe_open)
#if DO_NOT_COMPILE_ITC_MODULE_TEST == 0
    ,TEST_CASE(test_pipe_read)
#endif /* DO_NOT_COMPILE_ITC_MODULE_TEST */
TEST_LIST_END;
