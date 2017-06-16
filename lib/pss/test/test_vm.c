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

	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(uid), REG(0), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(entry, OPCODE(CLOSURE_NEW),  REG(0), REG(1), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(entry, OPCODE(DICT_NEW),  REG(0), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(0), REG(2), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(entry, OPCODE(SET_VAL), REG(1), REG(0), REG(2), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(entry, OPCODE(CALL), REG(1), REG(0), REG(2), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(entry, OPCODE(RETURN), REG(4), END), CLEANUP_NOP);

	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(ucom, OPCODE(DICT_NEW), REG(1), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(ucom, OPCODE(INT_LOAD), NUMERIC(0), REG(2), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(ucom, OPCODE(SET_VAL), REG(0), REG(1), REG(2), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(ucom, OPCODE(CALL), REG(0), REG(1), REG(2), END), CLEANUP_NOP);
	ASSERT_RETOK(pss_bytecode_addr_t, pss_bytecode_segment_append_code(ucom, OPCODE(RETURN), REG(2), END), CLEANUP_NOP);
	
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

	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(fid), REG(1), END), CLEANUP_NOP);      // Int-Load(foo)  r1
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(CLOSURE_NEW), REG(1), REG(0), END), CLEANUP_NOP);         // Make-Closure r1, r0
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(DICT_NEW), REG(1), END), CLEANUP_NOP);                    // Dict-New     r1
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(2), REG(2), END), CLEANUP_NOP);        // Int-Load(2)  r2
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(0), REG(3), END), CLEANUP_NOP);        // Int-Load(0)  r3
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(SET_VAL), REG(2), REG(1), REG(3), END), CLEANUP_NOP);     // Set-Val      r2, r1, r3
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(CALL), REG(0), REG(1), REG(2), END), CLEANUP_NOP);        // Call         r0, r1, r2
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(3), REG(3), END), CLEANUP_NOP);        // Int-Load(3)  r3
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(0), REG(4), END), CLEANUP_NOP);        // Int-load(0)  r4
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(SET_VAL), REG(3), REG(1), REG(4), END), CLEANUP_NOP);     // Set-Val      r3, r1, r4
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(CALL), REG(2), REG(1), REG(9), END), CLEANUP_NOP);        // Call         r2, r1, r9
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(INT_LOAD), NUMERIC(4), REG(3), END), CLEANUP_NOP);        // Int-Load(4)  r3
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(SET_VAL), REG(3), REG(1), REG(4), END), CLEANUP_NOP);     // Set-Val      r3, r1, r4
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(CALL), REG(9), REG(1), REG(10), END), CLEANUP_NOP);       // Call         r9, r1, r10
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(entry, OPCODE(RETURN), REG(10), END), CLEANUP_NOP);                     // Return       r10

	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(foo, OPCODE(INT_LOAD), NUMERIC(gid), REG(8), END), CLEANUP_NOP);      // Int-Load(goo)  r8
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(foo, OPCODE(CLOSURE_NEW), REG(8), REG(1), END), CLEANUP_NOP);           // Make-Closure goo, r2
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(foo, OPCODE(RETURN), REG(1), END), CLEANUP_NOP);                        // Return r2
	
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(goo, OPCODE(INT_LOAD), NUMERIC(kid), REG(8), END), CLEANUP_NOP);      // Int-Load(koo)  r8
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(goo, OPCODE(CLOSURE_NEW), REG(8), REG(1), END), CLEANUP_NOP);           // Make-Closure goo, r2
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(goo, OPCODE(RETURN), REG(1), END), CLEANUP_NOP);                        // Return r2

	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(koo, OPCODE(ADD), REG(0), REG(1), REG(3), END), CLEANUP_NOP);          // Add r0, r1, r2
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(koo, OPCODE(ADD), REG(3), REG(2), REG(3), END), CLEANUP_NOP);          // Add r0, r1, r2
	ASSERT_RETOK(pss_bytecode_addr_t,pss_bytecode_segment_append_code(koo, OPCODE(RETURN), REG(3), END), CLEANUP_NOP);                       // Return r2

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
int setup()
{
	ASSERT_OK(pss_log_set_write_callback(log_write_va), CLEANUP_NOP);
	ASSERT_OK(pss_init(), CLEANUP_NOP);

	return 0;
}

DEFAULT_TEARDOWN;

TEST_LIST_BEGIN
	TEST_CASE(test_currying),
	TEST_CASE(test_ucombinator)
TEST_LIST_END;
