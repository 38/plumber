/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <testenv.h>
#include <itc/module_types.h>
#include <module/test/module.h>

int set_get_request(void)
#if DO_NOT_COMPILE_ITC_MODULE_TEST == 0
{
	itc_module_type_t mod_test = itc_modtab_get_module_type_from_path("pipe.test.test");

	int rc = -1;
	static const char test_string[] = "this is a test incoming request";
	static const char test_string2[] = "this is a test response !!!!!";
	itc_module_pipe_param_t param = {
		.input_flags = RUNTIME_API_PIPE_INPUT,
		.output_flags = RUNTIME_API_PIPE_OUTPUT,
		.args = NULL
	};
	ASSERT_OK(module_test_set_request(test_string, sizeof(test_string)), CLEANUP_NOP);

	itc_module_pipe_t* request, *response;

	ASSERT(itc_module_pipe_accept(mod_test, param, &request, &response) == 1, CLEANUP_NOP);

	char rdata[1024];
	ASSERT(itc_module_pipe_read(rdata, sizeof(test_string), request) == sizeof(test_string), goto ERR);

	ASSERT_STREQ(test_string, rdata, goto ERR);

	ASSERT_RETOK(size_t, itc_module_pipe_write(test_string2, sizeof(test_string2), response), goto ERR);

	const char* resdata;
	ASSERT_PTR(resdata = module_test_get_response(), goto ERR);

	ASSERT_STREQ(test_string2, resdata, goto ERR);

	rc = 0;
ERR:
	if(request != NULL) itc_module_pipe_deallocate(request);
	if(response != NULL) itc_module_pipe_deallocate(response);
	return rc;
}
#else
{
	LOG_WARNING("Test case is disabled due to the test module is not compiled");
	return 0;
}
#endif /* DO_NOT_COMPILE_ITC_MODULE_TEST */

DEFAULT_SETUP;
DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(set_get_request)
TEST_LIST_END;
