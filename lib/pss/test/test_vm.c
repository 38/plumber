/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <testenv.h>
#include <pss.h>

#define OPCODE(x) PSS_BYTECODE_OPCODE_##x

#define NUMERIC(x) PSS_BYTECODE_ARG_NUMERIC((int64_t)x)

#define STRING(x) PSS_BYTECODE_ARG_STRING(x)

#define REG(x) PSS_BYTECODE_ARG_REGISTER(x)

#define LABEL(x) PSS_BYTECODE_ARG_LABEL(x)

#define END PSS_BYTECODE_ARG_END

#define CODE(seg, opcode, args...) ASSERT_RETOK(pss_bytecode_addr_t, _last_addr = pss_bytecode_segment_append_code(seg, OPCODE(opcode), ##args, END), CLEANUP_NOP)
pss_bytecode_addr_t _last_addr;

char buf[1024];
pss_value_t _builtin_print(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	(void)argc;
	pss_value_strify_to_buf(argv[0], buf, sizeof(buf));
	LOG_DEBUG("__builtin_print: %s", buf);
	pss_value_t ret = {};
	return ret;
}
pss_value_t _getter(const char* name)
{
	if(strcmp(name, "external_global") == 0)
	{
		pss_value_t value = {
			.kind = PSS_VALUE_KIND_NUM,
			.num  = 123456
		};
		return value;
	}

	pss_value_t undef = {};
	return undef;
}
int _setter(const char* name, pss_value_t value)
{
	if(strcmp(name, "external_global") == 0)
	{
		_builtin_print(NULL, 1, &value);
		return 1;
	}
	return 0;
}

int test_extension()
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t arg_entry[1] = {};
	pss_bytecode_segment_t* entry = pss_bytecode_segment_new(0, arg_entry);
	pss_bytecode_segid_t mid = pss_bytecode_module_append(module, entry);
	ASSERT_OK(pss_bytecode_module_set_entry_point(module, mid), CLEANUP_NOP);

	CODE(entry, STR_LOAD,    STRING("external_global"),    REG(0));
	CODE(entry, STR_LOAD,    STRING("__builtin_print"),    REG(1));
	CODE(entry, GLOBAL_GET,  REG(1),                       REG(1));
	CODE(entry, ARG,         REG(1)); 
	CODE(entry, CALL,        REG(1),                       REG(3));
	CODE(entry, GLOBAL_GET,  REG(0),                       REG(4));
	CODE(entry, INT_LOAD,    NUMERIC(123456),              REG(5));
	CODE(entry, EQ,          REG(5),                       REG(4),    REG(5));
	CODE(entry, GLOBAL_SET,  REG(4),                       REG(0));
	CODE(entry, RETURN,      REG(5));

	pss_bytecode_module_logdump(module);

	pss_vm_t* vm = pss_vm_new();
	ASSERT_PTR(vm, CLEANUP_NOP);
	ASSERT_OK(pss_vm_add_builtin_func(vm, "__builtin_print", _builtin_print), CLEANUP_NOP);
	pss_vm_external_global_ops_t ops = {
		.get = _getter,
		.set = _setter
	};
	ASSERT_OK(pss_vm_set_external_global_callback(vm, ops), CLEANUP_NOP);

	pss_value_t retval;

	ASSERT_OK(pss_vm_run_module(vm, module, &retval), CLEANUP_NOP);

	ASSERT(strcmp("123456", buf) == 0, CLEANUP_NOP);

	ASSERT(retval.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
	ASSERT(retval.num  == 1, CLEANUP_NOP);


	ASSERT_OK(pss_vm_free(vm), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);

	return 0;

}

int test_gcd()
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t arg_entry[1] = {};
	pss_bytecode_regid_t arg_foo[] = {2, 3};

	pss_bytecode_segment_t* entry = pss_bytecode_segment_new(0, arg_entry);
	ASSERT_PTR(entry, CLEANUP_NOP);
	pss_bytecode_segment_t* foo = pss_bytecode_segment_new(2, arg_foo);
	ASSERT_PTR(foo, CLEANUP_NOP);

	pss_bytecode_segid_t mid = pss_bytecode_module_append(module, entry);
	pss_bytecode_segid_t fid = pss_bytecode_module_append(module, foo);

	ASSERT_OK(pss_bytecode_module_set_entry_point(module, mid), CLEANUP_NOP);

	CODE(entry, INT_LOAD,     NUMERIC(fid),    REG(1));
	CODE(entry, CLOSURE_NEW,  REG(1),          REG(0));
	CODE(entry, STR_LOAD,     STRING("gcd"),   REG(1));
	CODE(entry, GLOBAL_SET,   REG(0),          REG(1));
	CODE(entry, INT_LOAD,     NUMERIC(120),    REG(3));
	CODE(entry, INT_LOAD,     NUMERIC(105),    REG(4));
	CODE(entry, ARG,          REG(3));
	CODE(entry, ARG,          REG(4));
	CODE(entry, CALL,         REG(0),          REG(2));
	CODE(entry, RETURN,       REG(2));

	pss_bytecode_label_t lret = pss_bytecode_segment_label_alloc(foo);
	ASSERT_RETOK(pss_bytecode_label_t, lret, CLEANUP_NOP);
	CODE(foo,   INT_LOAD,     REG(0),          LABEL(lret));
	CODE(foo,   JZ,           REG(2),          REG(0));
	CODE(foo,   MOD,          REG(3),          REG(2),      REG(0));   // R0 = R3 % R2
	CODE(foo,   ARG,          REG(0));
	CODE(foo,   ARG,          REG(2));
	CODE(foo,   STR_LOAD,     STRING("gcd"),   REG(5));
	CODE(foo,   GLOBAL_GET,   REG(5),          REG(5));
	CODE(foo,   CALL,         REG(5),          REG(5));
	CODE(foo,   RETURN,       REG(5));
	CODE(foo,   RETURN,       REG(3));
	ASSERT_OK(pss_bytecode_segment_patch_label(foo, lret, _last_addr), CLEANUP_NOP);

	pss_bytecode_module_logdump(module);

	pss_vm_t* vm = pss_vm_new();
	ASSERT_PTR(vm, CLEANUP_NOP);
	pss_value_t retval;

	ASSERT_OK(pss_vm_run_module(vm, module, &retval), CLEANUP_NOP);
	ASSERT(retval.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
	ASSERT(retval.num == 15, CLEANUP_NOP);

	ASSERT_OK(pss_vm_free(vm), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(retval), CLEANUP_NOP);

	return 0;


}

int test_generic_add()
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t arg_entry[1] = {};
	pss_bytecode_segment_t* entry = pss_bytecode_segment_new(0, arg_entry);
	ASSERT_PTR(entry, CLEANUP_NOP);
	pss_bytecode_segid_t mid = pss_bytecode_module_append(module, entry);
	ASSERT_OK(pss_bytecode_module_set_entry_point(module, mid), CLEANUP_NOP);

	CODE(entry, INT_LOAD,      NUMERIC(1),      REG(0));
	CODE(entry, INT_LOAD,      NUMERIC(2),      REG(1));
	CODE(entry, ADD,           REG(0),          REG(1),     REG(0));
	CODE(entry, STR_LOAD,      STRING("Hello"), REG(1));
	CODE(entry, ADD,           REG(1),          REG(0),     REG(0));
	CODE(entry, DICT_NEW,      REG(123));
	CODE(entry, SET_VAL,       REG(1),          REG(123),   REG(1));
	CODE(entry, ADD,           REG(0),          REG(123),   REG(0));
	CODE(entry, RETURN,        REG(0));

	pss_bytecode_module_logdump(module);

	pss_vm_t* vm = pss_vm_new();
	ASSERT_PTR(vm, CLEANUP_NOP);
	pss_value_t retval;

	ASSERT_OK(pss_vm_run_module(vm, module, &retval), CLEANUP_NOP);
	ASSERT(pss_value_ref_type(retval) == PSS_VALUE_REF_TYPE_STRING, CLEANUP_NOP);
	ASSERT_STREQ((const char*)pss_value_get_data(retval), "Hello3{ \"Hello\": \"Hello\" }", CLEANUP_NOP);

	ASSERT_OK(pss_vm_free(vm), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);
	ASSERT_OK(pss_value_decref(retval), CLEANUP_NOP);

	return 0;

}

int test_func_as_param()
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t arg_entry[1] = {};
	pss_bytecode_regid_t arg_foo[] = {2, 3};
	pss_bytecode_regid_t arg_goo[] = {2};

	pss_bytecode_segment_t* entry = pss_bytecode_segment_new(0, arg_entry);
	ASSERT_PTR(entry, CLEANUP_NOP);
	pss_bytecode_segment_t* foo = pss_bytecode_segment_new(2, arg_foo);
	ASSERT_PTR(foo, CLEANUP_NOP);
	pss_bytecode_segment_t* goo = pss_bytecode_segment_new(1, arg_goo);
	ASSERT_PTR(goo, CLEANUP_NOP);

	pss_bytecode_segid_t mid = pss_bytecode_module_append(module, entry);
	pss_bytecode_segid_t fid = pss_bytecode_module_append(module, foo);
	pss_bytecode_segid_t gid = pss_bytecode_module_append(module, goo);

	ASSERT_OK(pss_bytecode_module_set_entry_point(module, mid), CLEANUP_NOP);

	CODE(entry, INT_LOAD,    NUMERIC(gid), REG(2));
	CODE(entry, CLOSURE_NEW, REG(2),       REG(1));
	CODE(entry, INT_LOAD,    NUMERIC(fid), REG(2));
	CODE(entry, CLOSURE_NEW, REG(2),       REG(0));
	CODE(entry, INT_LOAD,    NUMERIC(2),   REG(4));
	CODE(entry, ARG,         REG(1));
	CODE(entry, ARG,         REG(4));
	CODE(entry, CALL,        REG(0),       REG(3));
	CODE(entry, RETURN,      REG(3));

	CODE(foo,   ARG,         REG(3));
	CODE(foo,   CALL,        REG(2),       REG(1));
	CODE(foo,   INT_LOAD,    NUMERIC(100), REG(0));
	CODE(foo,   ADD,         REG(0),       REG(1),    REG(2));
	CODE(foo,   RETURN,      REG(2));

	CODE(goo,   ADD,         REG(2),       REG(2),    REG(3));
	CODE(goo,   RETURN,      REG(3));

	pss_bytecode_module_logdump(module);

	pss_vm_t* vm = pss_vm_new();
	ASSERT_PTR(vm, CLEANUP_NOP);
	pss_value_t retval;

	ASSERT_OK(pss_vm_run_module(vm, module, &retval), CLEANUP_NOP);

	ASSERT(retval.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
	ASSERT(retval.num  == 104, CLEANUP_NOP);

	ASSERT_OK(pss_vm_free(vm), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);

	return 0;
}

int test_ucombinator()
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t arg_entry[1] = {};
	pss_bytecode_regid_t arg_u[1] = {0};

	pss_bytecode_segment_t* entry = pss_bytecode_segment_new(0, arg_entry);
	pss_bytecode_segment_t* ucom  = pss_bytecode_segment_new(1, arg_u);
	ASSERT_PTR(entry, CLEANUP_NOP);
	ASSERT_PTR(ucom, CLEANUP_NOP);

	pss_bytecode_segid_t eid = pss_bytecode_module_append(module, entry);
	pss_bytecode_segid_t uid = pss_bytecode_module_append(module, ucom);

	ASSERT_OK(pss_bytecode_module_set_entry_point(module, eid), CLEANUP_NOP);

	CODE(entry, INT_LOAD,     NUMERIC(uid), REG(0));
	CODE(entry, CLOSURE_NEW,  REG(0),       REG(1));
	CODE(entry, ARG,          REG(1));
	CODE(entry, CALL,         REG(1),       REG(2));
	CODE(entry, RETURN,       REG(4));

	CODE(ucom, ARG,           REG(0));
	CODE(ucom, CALL,          REG(0),       REG(2));
	CODE(ucom, RETURN,        REG(2));

	pss_bytecode_module_logdump(module);

	pss_vm_t* vm = pss_vm_new();
	ASSERT_PTR(vm, CLEANUP_NOP);
	ASSERT(ERROR_CODE(int) == pss_vm_run_module(vm, module, NULL), CLEANUP_NOP);

	pss_vm_exception_t* exception = pss_vm_last_exception(vm);
	ASSERT_PTR(exception, CLEANUP_NOP);
	ASSERT(exception->code == PSS_VM_ERROR_STACK, CLEANUP_NOP);
	ASSERT_OK(pss_vm_exception_free(exception), CLEANUP_NOP);

	ASSERT_OK(pss_vm_free(vm), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);
	return 0;
}

int test_currying()
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t arg_entry[1] = {};
	pss_bytecode_regid_t arg_foo[1] = {0};
	pss_bytecode_regid_t arg_goo[1] = {1};
	pss_bytecode_regid_t arg_koo[1] = {2};

	pss_bytecode_segment_t* entry = pss_bytecode_segment_new(0, arg_entry);
	ASSERT_PTR(entry, CLEANUP_NOP);
	pss_bytecode_segment_t* foo = pss_bytecode_segment_new(1, arg_foo);
	ASSERT_PTR(foo, CLEANUP_NOP);
	pss_bytecode_segment_t* goo = pss_bytecode_segment_new(1, arg_goo);
	ASSERT_PTR(goo, CLEANUP_NOP);
	pss_bytecode_segment_t* koo = pss_bytecode_segment_new(1, arg_koo);
	ASSERT_PTR(goo, CLEANUP_NOP);

	pss_bytecode_segid_t mid = pss_bytecode_module_append(module, entry);
	pss_bytecode_segid_t fid = pss_bytecode_module_append(module, foo);
	pss_bytecode_segid_t gid = pss_bytecode_module_append(module, goo);
	pss_bytecode_segid_t kid = pss_bytecode_module_append(module, koo);

	ASSERT_OK(pss_bytecode_module_set_entry_point(module, mid), CLEANUP_NOP);

	CODE(entry, INT_LOAD,    NUMERIC(fid),   REG(1));
	CODE(entry, CLOSURE_NEW, REG(1),         REG(0));
	CODE(entry, INT_LOAD,    NUMERIC(2),     REG(2));
	CODE(entry, ARG,         REG(2));
	CODE(entry, CALL,        REG(0),         REG(2));
	CODE(entry, INT_LOAD,    NUMERIC(3),     REG(3));
	CODE(entry, ARG,         REG(3));
	CODE(entry, CALL,        REG(2),         REG(9));
	CODE(entry, INT_LOAD,    NUMERIC(4),     REG(3));
	CODE(entry, ARG,         REG(3));
	CODE(entry, CALL,        REG(9),         REG(10));
	CODE(entry, RETURN,      REG(10));

	CODE(foo, INT_LOAD,      NUMERIC(gid),   REG(8));
	CODE(foo, CLOSURE_NEW,   REG(8),         REG(1));
	CODE(foo, RETURN,        REG(1));

	CODE(goo, INT_LOAD,      NUMERIC(kid),   REG(8));
	CODE(goo, CLOSURE_NEW,   REG(8),         REG(1));
	CODE(goo, RETURN,        REG(1));

	CODE(koo, ADD,           REG(0),         REG(1),   REG(3));
	CODE(koo, ADD,           REG(3),         REG(2),   REG(3));
	CODE(koo, RETURN,        REG(3));

	pss_bytecode_module_logdump(module);

	pss_vm_t* vm = pss_vm_new();

	ASSERT_PTR(vm, CLEANUP_NOP);

	pss_value_t ret;

	ASSERT_OK(pss_vm_run_module(vm, module, &ret), CLEANUP_NOP);
	ASSERT(ret.kind == PSS_VALUE_KIND_NUM, CLEANUP_NOP);
	ASSERT(ret.num == 9, CLEANUP_NOP);

	ASSERT_OK(pss_vm_free(vm), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);

	return 0;
}

int test_first_class_func()
{
	pss_bytecode_module_t* module = pss_bytecode_module_new();
	ASSERT_PTR(module, CLEANUP_NOP);

	pss_bytecode_regid_t arg_entry[1] = {};
	
	pss_bytecode_segment_t* entry = pss_bytecode_segment_new(0, arg_entry);
	ASSERT_PTR(entry, CLEANUP_NOP);
	
	pss_bytecode_segid_t mid = pss_bytecode_module_append(module, entry);

	ASSERT_OK(pss_bytecode_module_set_entry_point(module, mid), CLEANUP_NOP);

	CODE(entry, INT_LOAD,    NUMERIC(mid),   REG(2));
	CODE(entry, CLOSURE_NEW, REG(2),     REG(2));
	CODE(entry, DICT_NEW,    REG(1));
	CODE(entry, SET_VAL,     REG(2),     REG(1),     REG(3));
	CODE(entry, RETURN,      REG(0));


	pss_bytecode_module_logdump(module);

	pss_vm_t* vm = pss_vm_new();

	ASSERT_PTR(vm, CLEANUP_NOP);
	ASSERT_OK(pss_vm_run_module(vm, module, NULL), CLEANUP_NOP);
	ASSERT_OK(pss_vm_free(vm), CLEANUP_NOP);
	ASSERT_OK(pss_bytecode_module_free(module), CLEANUP_NOP);

	return 0;
}

int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);
	ASSERT_OK(pss_init(), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
    TEST_CASE(test_currying),
    TEST_CASE(test_ucombinator),
    TEST_CASE(test_func_as_param),
    TEST_CASE(test_generic_add),
    TEST_CASE(test_gcd),
    TEST_CASE(test_extension),
	TEST_CASE(test_first_class_func)
TEST_LIST_END;
