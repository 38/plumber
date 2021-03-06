/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <jsonschema.h>
#include <jsonschema/log.h>

jsonschema_t* schema = NULL;

int test_schema_compile(void)
{
	const char* schema_text = "{\n"
	"	\"name\": \"string(6,128)\",\n"
	"	\"nickname\": \"string(6,16)|null\",\n"
	"	\"address\": {\n"
	"		\"__schema_property__\": \"nullable\",\n"
	"		\"street\": \"string(0,32)\",\n"
	"		\"room_number\": \"string|null\",\n"
	"		\"city\": \"string\",\n"
	"		\"state\": \"string\",\n"
	"		\"country\": \"string\",\n"
	"		\"zipcode\": \"string(0,8)|int(0,1000000)\"\n"
	"	},\n"
	"	\"items\": [{\n"
	"		\"code\": \"string\",\n"
	"		\"count\": \"int(0,0x7fffffff)\",\n"
	"		\"unit_price\": \"float(0, 1e+100)\"\n"
	"	}, \"*\"]\n"
	"}";
	ASSERT_PTR(schema = jsonschema_from_string(schema_text), CLEANUP_NOP);
	return 0;
}

int test_schema_validate_valid(void)
{
	const char* value_text[] = {
		"{\"name\": \"plumber\", \"nickname\": \"plumber\", \"items\": [] }",
		"{\"name\": \"plumber\", \"nickname\": null, \"address\":{\"state\":"" \"UT\", \"city\": \"SLC\", \"street\": \"howick\", \"country\":\"US\", \"zipcode\":123456}, \"items\": [] }",
		"{\"name\": \"plumber\", \"nickname\": null, \"address\":{\"state\": \"UT\", \"city\": \"SLC\", \"street\": \"howick\", \"country\":\"US\", \"zipcode\":123456}, \"items\": [{\"code\": \"testobjectid\", \"count\": 123, \"unit_price\": 1.23}, {\"code\":\"xxxxx\", \"count\": 1, \"unit_price\": 1e+10}] }"
	};

	uint32_t i;
	for(i = 0; i < sizeof(value_text) / sizeof(value_text[0]); i ++)
		ASSERT(1 == jsonschema_validate_str(schema, value_text[i], 0), CLEANUP_NOP);

	return 0;
}


int test_schema_validate_invalid(void)
{
	const char* value_text[] = {
		"{\"name\": \"bad\", \"nickname\": \"plumber\", \"items\": [] }",
		"{\"name\": \"plumber\", \"nickname\": \"12345678901234567890\", \"items\": [] }",
		"{\"name\": \"plumber\", \"address\":{}, \"items\": [] }",
		"{\"name\": \"plumber\", \"address\":null, \"items\": [1] }",
		"{}",
		"null",
		"{\"name\": \"plumber\", \"nickname\": null, \"address\":{\"state\":"" \"UT\", \"city\": \"SLC\", \"street\": \"howick\", \"country\":\"US\", \"zipcode\":-123456}, \"items\": [] }"
	};

	uint32_t i;
	for(i = 0; i < sizeof(value_text) / sizeof(value_text[0]); i ++)
		ASSERT(0 == jsonschema_validate_str(schema, value_text[i], 0), CLEANUP_NOP);

	return 0;
}

int test_schema_update(void)
{
	const char* original = "{\"name\": \"plumber\", \"items\": [] }";
	char outbuf[1024];

	const char* patch = "{\"name\": \"plumber service framework\", \"nickname\" : \"plumber\", \"items\": {\"__insertion__\":[[0, {\"code\":\"xxxxx\", \"count\": 1, \"unit_price\": 1e+10}]]}}";

	size_t sz = jsonschema_update_str(schema, NULL, 0, original, 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, original, 0, patch, 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, outbuf, 0, patch, 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, outbuf, 0, "{\"items\":{\"__deletion__\":[0], \"0\":{\"unit_price\":1.0}}}", 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, outbuf, 0, "{\"items\":[]}", 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, outbuf, 0, "{\"__complete_type__\":true, \"name\": \"plumber v0.1\", \"nickname\": \"plumber\", \"items\": [] }", 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, outbuf, 0, "{\"address\": {\"state\":"" \"UT\", \"city\": \"SLC\", \"street\": \"howick\", \"country\":\"US\", \"zipcode\":123456}}", 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, outbuf, 0, "{\"address\": null}", 0, outbuf, sizeof(outbuf));

	ASSERT_RETOK(size_t, sz, CLEANUP_NOP);

	LOG_DEBUG("%s", outbuf);

	sz = jsonschema_update_str(schema, outbuf, 0, "{\"name\": null}", 0, outbuf, sizeof(outbuf));

	ASSERT(ERROR_CODE(size_t) == sz, CLEANUP_NOP);

	return 0;

}

int setup(void)
{
	if(ERROR_CODE(int) == jsonschema_log_set_write_callback(log_write_va))
		ERROR_RETURN_LOG(int, "Cannot set the log write callback");
	/* For the libstdc++ emergency_pool, we could call __gnu_cxx::__freeres if possible */
	expected_memory_leakage();
	return 0;
}

int teardown(void)
{
	ASSERT(NULL == schema || ERROR_CODE(int) != jsonschema_free(schema), CLEANUP_NOP);
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(test_schema_compile),
    TEST_CASE(test_schema_validate_valid),
    TEST_CASE(test_schema_validate_invalid),
    TEST_CASE(test_schema_update)
TEST_LIST_END;

