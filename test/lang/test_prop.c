#include <testenv.h>
#include <utils/string.h>
char prop_list[1024][1024];
uint32_t sym[1024];
lang_bytecode_table_t *bc_table;
lang_prop_callback_vector_t *vec;
lang_prop_callback_vector_t *v2;
lang_prop_callback_vector_t *v3;
uint32_t add_sym;
const char* additional_symbol = "dummyfield323.dummyfield222222222222222";
const char* additional_symbol_value = "dummyfield323.dummyfield222222222222222.dummy_value1";
static int _getter(const lang_prop_callback_vector_t* vec, const void* param, uint32_t nsec, const uint32_t* symbol, lang_prop_type_t type, void* buffer)
{
	ASSERT_PTR(vec, CLEANUP_NOP);
	ASSERT_PTR(symbol, CLEANUP_NOP);
	ASSERT_PTR(buffer, CLEANUP_NOP);
	ASSERT(nsec == 1, CLEANUP_NOP);
	ASSERT(symbol[0] == 7, CLEANUP_NOP);
	ASSERT_PTR(param, CLEANUP_NOP);
	(void)type;
	return 1;
}
static int _setter(const lang_prop_callback_vector_t* vec, const void* param, uint32_t nsec, const uint32_t* symbol, lang_prop_type_t type, const void* buffer)
{
	ASSERT_PTR(vec, CLEANUP_NOP);
	ASSERT_PTR(symbol, CLEANUP_NOP);
	ASSERT_PTR(buffer, CLEANUP_NOP);
	ASSERT_PTR(param, CLEANUP_NOP);
	ASSERT(type == LANG_PROP_TYPE_INTEGER, CLEANUP_NOP);
	ASSERT(nsec == 1, CLEANUP_NOP);
	ASSERT(symbol[0] == 7, CLEANUP_NOP);
	ASSERT(*(int32_t*)buffer == 123, CLEANUP_NOP);
	return 1;
}
int registration()
{
	size_t i;
	for(i = 0; i < sizeof(prop_list) / sizeof(prop_list[0]); i ++)
	{
		lang_prop_callback_t cb = {
			.get = _getter,
			.set = _setter,
			.param = prop_list + i,
			.symbol_prefix = prop_list[i]
		};

		ASSERT_OK(lang_prop_register_callback(&cb), CLEANUP_NOP);
	}
	return 0;
}
int creation()
{
	ASSERT_PTR(vec = lang_prop_callback_vector_new(bc_table), CLEANUP_NOP);
	return 0;
}
int getter()
{
	size_t i;
	for(i = 0; i < sizeof(prop_list) / sizeof(prop_list[0]); i ++)
	{
		string_buffer_t sb;
		static char buffer[2048];
		string_buffer_open(buffer ,sizeof(buffer), &sb);
		string_buffer_appendf(&sb, "%s.dummy_value1", prop_list[i]);
		ASSERT_PTR(string_buffer_close(&sb), CLEANUP_NOP);
		lang_prop_value_t val;
		lang_prop_type_t type;
		int32_t v = 123;
		ASSERT(1 == lang_prop_get(vec, sym[i], &type, &val), CLEANUP_NOP);
		ASSERT(1 == lang_prop_set(vec, sym[i], LANG_PROP_TYPE_INTEGER, &v), CLEANUP_NOP);

		ASSERT(1 == lang_prop_get(v2, sym[i], &type, &val), CLEANUP_NOP);
		ASSERT(1 == lang_prop_set(v2, sym[i], LANG_PROP_TYPE_INTEGER, &v), CLEANUP_NOP);
	}
	return 0;
}
int update()
{
	lang_prop_callback_t cb = {
		.get = _getter,
		.set = _setter,
		.param = additional_symbol,
		.symbol_prefix = additional_symbol
	};

	ASSERT_OK(lang_prop_register_callback(&cb), CLEANUP_NOP);

	return 0;
}
int getter2()
{
	ASSERT_OK(getter(), CLEANUP_NOP);
	lang_prop_value_t val;
	lang_prop_type_t  type;
	int32_t v = 123;
	ASSERT(1 == lang_prop_get(vec, add_sym, &type, &val), CLEANUP_NOP);
	ASSERT(1 == lang_prop_set(vec, add_sym, LANG_PROP_TYPE_INTEGER, &v), CLEANUP_NOP);

	ASSERT(1 == lang_prop_get(v2, add_sym, &type, &val), CLEANUP_NOP);
	ASSERT(1 == lang_prop_set(v2, add_sym, LANG_PROP_TYPE_INTEGER, &v), CLEANUP_NOP);

	ASSERT_PTR(v3 = lang_prop_callback_vector_new(bc_table), CLEANUP_NOP);

	size_t i;
	for(i = 0; i < sizeof(prop_list) / sizeof(prop_list[0]); i ++)
	{
		string_buffer_t sb;
		static char buffer[2048];
		string_buffer_open(buffer ,sizeof(buffer), &sb);
		string_buffer_appendf(&sb, "%s.dummy_value1", prop_list[i]);
		ASSERT_PTR(string_buffer_close(&sb), CLEANUP_NOP);
		lang_prop_value_t val;
		lang_prop_type_t type;
		int32_t v = 123;
		ASSERT(1 == lang_prop_get(v3, sym[i], &type, &val), CLEANUP_NOP);
		ASSERT(1 == lang_prop_set(v3, sym[i], LANG_PROP_TYPE_INTEGER, &v), CLEANUP_NOP);
	}

	return 0;
}
int setup()
{
	ASSERT_PTR(bc_table = lang_bytecode_table_new(), CLEANUP_NOP);
	size_t i;
	for(i = 0; i < sizeof(prop_list) / sizeof(prop_list[0]); i ++)
	{
		if(i == sizeof(prop_list) / sizeof(prop_list[0]) / 2)
		{
			ASSERT_RETOK(uint32_t, lang_bytecode_table_insert_symbol(bc_table, additional_symbol), CLEANUP_NOP);
			ASSERT_RETOK(uint32_t, add_sym = lang_bytecode_table_insert_symbol(bc_table, additional_symbol_value), CLEANUP_NOP);
		}
		string_buffer_t sb;
		string_buffer_open(prop_list[i], sizeof(prop_list[i]), &sb);
		size_t j;
		size_t nsec = 2 + (13 * i * i + 3 * i + 5) % 13;
		for(j = 0; j < nsec; j ++)
		{
			size_t num = ((i + 1) * (j + 1) + (j + 1) * 1009) % (10 + j * 1003);
			string_buffer_appendf(&sb, "dummyfield%zu", num);
			if(j != nsec - 1) string_buffer_append(".", &sb);
		}
		ASSERT_PTR(string_buffer_close(&sb), CLEANUP_NOP);
		static char buffer[2048];
		string_buffer_open(buffer ,sizeof(buffer), &sb);
		string_buffer_appendf(&sb, "%s.dummy_value1", prop_list[i]);
		ASSERT_PTR(string_buffer_close(&sb), CLEANUP_NOP);
		ASSERT_RETOK(uint32_t,          lang_bytecode_table_insert_symbol(bc_table, prop_list[i]), CLEANUP_NOP);
		ASSERT_RETOK(uint32_t, sym[i] = lang_bytecode_table_insert_symbol(bc_table, buffer), CLEANUP_NOP);
		LOG_DEBUG("Property %zu: %s", i, prop_list[i]);
	}

	ASSERT_PTR(v2 = lang_prop_callback_vector_new(bc_table), CLEANUP_NOP);

	return 0;
}

int teardown()
{
	ASSERT_OK(lang_bytecode_table_free(bc_table), CLEANUP_NOP);
	ASSERT_OK(lang_prop_callback_vector_free(vec), CLEANUP_NOP);
	ASSERT_OK(lang_prop_callback_vector_free(v2), CLEANUP_NOP);
	ASSERT_OK(lang_prop_callback_vector_free(v3), CLEANUP_NOP);
	return 0;
}

TEST_LIST_BEGIN
    TEST_CASE(registration),
    TEST_CASE(creation),
    TEST_CASE(getter),
    TEST_CASE(update),
    TEST_CASE(getter2)
TEST_LIST_END;
