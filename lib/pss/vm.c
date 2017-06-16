/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <error.h>

#include <utils/hash/murmurhash3.h>

#include <package_config.h>
#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>
#include <pss/dict.h>
#include <pss/string.h>
#include <pss/closure.h>
#include <pss/vm.h>

static const char* _errstr[] = {
	[PSS_VM_ERROR_NONE]       "Success",
	[PSS_VM_ERROR_BYTECODE]   "Invalid Bytecode",
	[PSS_VM_ERROR_TYPE]       "Type Error",
	[PSS_VM_ERROR_INTERNAL]   "Interpreter Interal Error",
	[PSS_VM_ERROR_ARITHMETIC] "Arithmetic Error",
	[PSS_VM_ERROR_STACK]      "Stack Overflow"
};

/**
 * @brief Represents one frame on the stack
 **/
typedef struct _stack_t {
	pss_vm_t*                     host;   /*! The host VM */
	const pss_bytecode_module_t*  module; /*!< Current module */
	const pss_bytecode_segment_t* code;   /*!< The code segment that is currently running */
	pss_frame_t*                  frame;  /*!< The register frame for current stack frame */
	pss_bytecode_addr_t           ip;     /*!< The instruction pointer - The address to the next bytecode to execute */
	uint32_t                      line;   /*!< Current line number */
	const char*                   func;   /*!< Current function name */
	struct _stack_t*              next;   /*!< The next frame in the stack */
} _stack_t;

/**
 * @brief The actual data structure for a PSS Virtual Machine
 **/
struct _pss_vm_t {
	pss_vm_error_t error;       /*!< The error code if the VM is entering an error state */
	uint32_t       level;       /*!< The stack level */
	_stack_t*      stack;       /*!< The stack we are using */
	pss_dict_t*    global;      /*!< The global variable table */
};

/**
 * @brief Create a new stack frame
 * @param host The host VM
 * @param closure The colsure to run
 * @param args The argument dictionary
 * @return The newly created stack
 **/
static inline _stack_t* _stack_new(pss_vm_t* host, const pss_closure_t* closure, const pss_dict_t* args)
{
	_stack_t* ret = (_stack_t*)calloc(1, sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the stack frame");

	ret->host = host;
	if(NULL == (ret->code = pss_closure_get_code(closure)))
		ERROR_LOG_GOTO(ERR, "Cannot get the code segment from the closure");
	if(NULL == (ret->module = pss_closure_get_module(closure)))
		ERROR_LOG_GOTO(ERR, "Cannot get the module contains the closure");
	ret->ip = 0;
	ret->line = 0;
	ret->func = NULL;
	ret->next = NULL;

	pss_bytecode_regid_t const* arg_regs;
	int argc = pss_bytecode_segment_get_args(ret->code, &arg_regs);
	if(ERROR_CODE(int) == argc)
		ERROR_LOG_GOTO(ERR, "Cannot get the number of arguments of the code segment");

	int dict_size = args == NULL ? 0 : (int)pss_dict_size(args);
	if(ERROR_CODE(int) == dict_size)
		ERROR_LOG_GOTO(ERR, "Cannot get the size of the argument list");

	if(NULL == (ret->frame = pss_closure_get_frame(closure)))
		ERROR_LOG_GOTO(ERR, "Cannot duplicate the environment frame");

	if(argc > dict_size) argc = dict_size;

	int i;
	for(i = 0; i < argc; i ++)
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", i);
		pss_value_t value = pss_dict_get(args, buf);

		if(PSS_VALUE_KIND_ERROR == value.kind)
			ERROR_LOG_GOTO(ERR, "Cannot get the value of argument %d", i);

		if(ERROR_CODE(int) == pss_frame_reg_set(ret->frame, arg_regs[i], value))
			ERROR_LOG_GOTO(ERR, "Cannot set the value of register %u", arg_regs[i]);
	}

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->frame) pss_frame_free(ret->frame);
		free(ret);
	}

	return NULL;

}

/**
 * @brief Dispose an used stack frame
 * @param stack The stack we want to use
 * @return status code
 **/
static inline int _stack_free(_stack_t* stack)
{
	int rc = 0;
	if(NULL != stack->frame && ERROR_CODE(int) == pss_frame_free(stack->frame))
	{
		LOG_ERROR("Cannot dispose the register frame");
		rc = ERROR_CODE(int);
	}

	free(stack);
	return rc;
}

/**
 * @brief Read the register from the current instruction on current stack frame
 * @param stack The current stack frame
 * @param inst The instruction
 * @param idx The operand index
 * @return The value read from the frame
 **/
static inline pss_value_t _read_reg(pss_vm_t* vm, const pss_bytecode_instruction_t* inst, uint32_t idx)
{
	pss_bytecode_regid_t regid = inst->reg[idx];
	pss_value_t val = pss_frame_reg_get(vm->stack->frame, regid);
	if(val.kind == PSS_VALUE_KIND_ERROR)
		vm->error = PSS_VM_ERROR_INTERNAL;
	return val;
}

/**
 * @brief Check if the value is the expected kind, if not raise an type error and return 0
 * @param vm The virtual machine
 * @param value The value to check
 * @param kind The expected kind
 * @param raise If we want to raise an error
 * @return the check result
 **/
static inline int _is_value_kind(pss_vm_t* vm, pss_value_t value, pss_value_kind_t kind, int raise)
{
	if(vm->error != PSS_VM_ERROR_NONE) return 0;
	if(value.kind != kind) 
	{
		if(raise) vm->error = PSS_VM_ERROR_TYPE;
		return 0;
	}

	return 1;
}

/**
 * @brief Get the internal data of the reference type, if type isn't match return NULL and if raise is set, raise an type error
 * @param vm The virtual machine
 * @param value The value to check
 * @param type The type code
 * @param raise If we want to raise an type error
 * @return The internal data
 **/
static inline void* _value_get_ref_data(pss_vm_t* vm, pss_value_t value, pss_value_ref_type_t type, int raise)
{
	if(!_is_value_kind(vm, value, PSS_VALUE_KIND_REF, raise)) 
		return NULL;

	if(type != pss_value_ref_type(value))
	{
		if(raise) vm->error = PSS_VM_ERROR_TYPE;
		return NULL;
	}

	return pss_value_get_data(value);
}
/**
 * @brief Force convert the value to string
 * @param vm The virtual machine
 * @param value The value to convert
 * @param buf The buffer
 * @param sz The size 
 * @return The converted string
 **/
static inline const char* _get_string_repr(pss_vm_t* vm, pss_value_t value, char* buf, size_t sz)
{
	if(value.kind == PSS_VALUE_KIND_ERROR) return NULL; 
	const char* ret = _value_get_ref_data(vm, value, PSS_VALUE_REF_TYPE_STRING, 0);

	if(NULL != ret) return ret;

	if(ERROR_CODE(size_t) == pss_value_strify_to_buf(value, buf, sz))
	{
		vm->error = PSS_VM_ERROR_INTERNAL;
		return NULL;
	}

	return buf;
}

/**
 * @brief Execute the insttructions that creates an object
 * @param vm The virtual machine
 * @param inst The instruction
 * @return status code
 **/
static inline int _exec_new(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_bytecode_regid_t target = inst->reg[inst->info->num_regs - 1];
	pss_value_t val;
	switch(inst->info->rtype)
	{
		case PSS_BYTECODE_RTYPE_DICT:
			val = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);
			break;
		case PSS_BYTECODE_RTYPE_CLOSURE:
		{
			pss_value_t segval = _read_reg(vm, inst, 0);
			if(!_is_value_kind(vm, segval, PSS_VALUE_KIND_NUM, 1))
				return 0;

			pss_closure_creation_param_t param = {
				.segid  = (pss_bytecode_segid_t)segval.num,
				.module = vm->stack->module,
				.env    = vm->stack->frame
			};

			val = pss_value_ref_new(PSS_VALUE_REF_TYPE_CLOSURE, &param);
			break;
		}
		default:
			vm->error = PSS_VM_ERROR_BYTECODE;
			return 0;
	}

	if(PSS_VALUE_KIND_ERROR == val.kind)
		ERROR_RETURN_LOG(int, "Cannot create the requested value");

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, target, val))
	{
		pss_value_decref(val);
		ERROR_RETURN_LOG(int, "Cannot modify the current frame");
	}

	return 0;
}

static inline int _exec_load(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_bytecode_regid_t target = inst->reg[inst->info->num_regs - 1];
	pss_value_t val = {};
	switch(inst->info->rtype)
	{
		case PSS_BYTECODE_RTYPE_INT:
			val.kind = PSS_VALUE_KIND_NUM;
			val.num = inst->num;
			break;
		case PSS_BYTECODE_RTYPE_STR:
		{
			const char* str = inst->str;
			char* runtime_str = pss_string_concat(str, "");
			if(NULL == runtime_str) ERROR_RETURN_LOG(int, "Cannot create a runtime string");
			val = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, runtime_str);
			if(val.kind == PSS_VALUE_KIND_ERROR) free(runtime_str);
		}
		default:
			vm->error = PSS_VM_ERROR_BYTECODE;
			return 0;
	}

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, target, val))
	{
		pss_value_decref(val);
		ERROR_RETURN_LOG(int, "Cannot modify the current frame");
	}

	return 0;
}

static inline int _exec_setval(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_dict_t* dict = (pss_dict_t*)_value_get_ref_data(vm, _read_reg(vm, inst, 1), PSS_VALUE_REF_TYPE_DICT, 1);
	if(NULL == dict) return 0;

	char buf[4096];

	const char* key = _get_string_repr(vm, _read_reg(vm, inst, 2), buf, sizeof(buf));
	if(NULL == key) ERROR_RETURN_LOG(int, "Cannot convert the key to string");

	pss_value_t value = _read_reg(vm, inst, 0);
	if(value.kind == PSS_VALUE_KIND_ERROR) 
		ERROR_RETURN_LOG(int, "Cannot read the source register");

	if(ERROR_CODE(int) == pss_dict_set(dict, key, value))
		ERROR_RETURN_LOG(int, "Cannot insert the value to the dictionary");

	return 0;
}

static inline int _exec_add(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t left = _read_reg(vm, inst, 0);
	pss_value_t right = _read_reg(vm, inst, 1);
	pss_value_t result = {};

	if(_is_value_kind(vm, left, PSS_VALUE_KIND_NUM, 0) &&
	   _is_value_kind(vm, right, PSS_VALUE_KIND_NUM, 0))
	{
		result.kind = PSS_VALUE_KIND_NUM;
		result.num = left.num + right.num;
	}
	else
	{
		char leftbuf[4096];
		char rightbuf[4096];
		const char* left_str = _get_string_repr(vm, left, leftbuf, sizeof(leftbuf));
		if(NULL == left_str) ERROR_RETURN_LOG(int, "Cannot convert the left operand to string");

		const char* right_str = _get_string_repr(vm, right, rightbuf, sizeof(rightbuf));
		if(NULL == right_str) ERROR_RETURN_LOG(int, "Cannot convert the right operand to string");

		char* result_str = pss_string_concat(left_str, right_str);
		if(NULL == result_str)
			ERROR_RETURN_LOG(int, "Cannot concate two string");

		result = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, result_str);
		if(result.kind == PSS_VALUE_KIND_ERROR)
		{
			free(result_str);
			ERROR_RETURN_LOG(int, "Cannot create string value");
		}
	}

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[2], result))
	{
		pss_value_decref(result);
		ERROR_RETURN_LOG(int, "Cannot set the register value");
	}

	return 0;
}

static inline pss_bytecode_regid_t _exec(pss_vm_t* vm);
static inline int _exec_call(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	const pss_closure_t* closure = (const pss_closure_t*)_value_get_ref_data(vm, _read_reg(vm, inst, 0), PSS_VALUE_REF_TYPE_CLOSURE, 1);
	if(NULL == closure) return 0;

	const pss_dict_t* args = (const pss_dict_t*)_value_get_ref_data(vm, _read_reg(vm, inst, 1), PSS_VALUE_REF_TYPE_DICT, 1);
	if(NULL == args) return 0;

	_stack_t* this_stack = vm->stack;

	_stack_t* new_stack = _stack_new(vm, closure, args);
	if(NULL == new_stack) ERROR_RETURN_LOG(int, "Cannot create new stack frame");

	new_stack->next = this_stack;
	vm->stack = new_stack;

	pss_bytecode_regid_t ret;

	if(ERROR_CODE(pss_bytecode_regid_t) == (ret = _exec(vm)))
		return 0;

	pss_value_t retval = pss_frame_reg_get(vm->stack->frame, ret);
	if(retval.kind == PSS_VALUE_KIND_ERROR)
		ERROR_RETURN_LOG(int, "Cannot fetch the return value of the last function call");

	if(ERROR_CODE(int) == pss_frame_reg_set(this_stack->frame, inst->reg[2], retval))
	{
		LOG_ERROR("Cannot set the result register");
		/* In this case, we should preserve the stack */
		return ERROR_CODE(int);
	}

	if(ERROR_CODE(int) == _stack_free(vm->stack))
		ERROR_RETURN_LOG(int, "Cannot dispose the stack");

	vm->stack = this_stack;

	return 0;
}

static inline pss_bytecode_regid_t _exec(pss_vm_t* vm)
{
	pss_bytecode_regid_t retreg = ERROR_CODE(pss_bytecode_regid_t);
	_stack_t* top = vm->stack;
	vm->level ++;

	if(vm->level > PSS_VM_STACK_LIMIT) vm->error = PSS_VM_ERROR_STACK;

	while(vm->stack != NULL && PSS_VM_ERROR_NONE == vm->error && retreg == ERROR_CODE(pss_bytecode_regid_t))
	{
		pss_bytecode_instruction_t inst;
		if(ERROR_CODE(int) == pss_bytecode_segment_get_inst(top->code, top->ip, &inst))
		{
			LOG_ERROR("Cannot fetch instruction at address 0x%x", top->ip);
			vm->error = PSS_VM_ERROR_INTERNAL;
			break;
		}

		int rc = 0;
		switch(inst.info->operation)
		{
			case PSS_BYTECODE_OP_NEW:
				rc = _exec_new(vm, &inst);
				break;
			case PSS_BYTECODE_OP_LOAD:
				rc = _exec_load(vm, &inst);
				break;
			case PSS_BYTECODE_OP_SETVAL:
				rc = _exec_setval(vm, &inst);
				break;
			case PSS_BYTECODE_OP_ADD:
				rc = _exec_add(vm, &inst);
				break;
			case PSS_BYTECODE_OP_CALL:
				rc = _exec_call(vm, &inst);
				break;
			case PSS_BYTECODE_OP_RETURN:
				retreg = inst.reg[0];
				break;
			default:
				rc = ERROR_CODE(int);
				LOG_ERROR("Invalid opration code %u", inst.info->operation);
		}

		if(rc == ERROR_CODE(int))
			vm->error = PSS_VM_ERROR_INTERNAL;

		top->ip ++;
	}
	vm->level --;

	return retreg; 
}

pss_vm_t* pss_vm_new()
{
	pss_vm_t* ret = (pss_vm_t*)calloc(1, sizeof(*ret));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the VM");

	if(NULL == (ret->global = pss_dict_new()))
		ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the global storage");

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->global) pss_dict_free(ret->global);
		free(ret);
	}

	return NULL;
}

int pss_vm_free(pss_vm_t* vm)
{
	int rc = 0;
	if(NULL == vm) ERROR_RETURN_LOG(int, "Invalid arguments");

	_stack_t* ptr;
	for(ptr = vm->stack; NULL != ptr;)
	{
		_stack_t* this = ptr;
		ptr = ptr->next;

		if(ERROR_CODE(int) == _stack_free(this))
		{
			LOG_ERROR("Cannot dispose stack frame");
			rc = ERROR_CODE(int);
		}
	}

	if(ERROR_CODE(int) == pss_dict_free(vm->global))
	{
		LOG_ERROR("Cannot dispose the global storage");
		rc = ERROR_CODE(int);
	}

	free(vm);

	return rc;
}

int pss_vm_run_module(pss_vm_t* vm, const pss_bytecode_module_t* module, pss_value_t* retbuf)
{
	if(NULL == vm || NULL == module)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(vm->error != PSS_VM_ERROR_NONE)
		ERROR_RETURN_LOG(int, "VM is in exception state");

	pss_bytecode_segid_t segid = pss_bytecode_module_get_entry_point(module);
	if(ERROR_CODE(pss_bytecode_segid_t) == segid)
		ERROR_RETURN_LOG(int, "Cannot get the entry point segment ID");

	pss_closure_creation_param_t param = {
		.module = module,
		.segid  = segid,
		.env    = NULL
	};

	pss_value_t closure_value = pss_value_ref_new(PSS_VALUE_REF_TYPE_CLOSURE, &param);
	const pss_closure_t* closure = (const pss_closure_t*)pss_value_get_data(closure_value);

	if(NULL == closure) ERROR_RETURN_LOG(int, "Cannot create closure for the entry point");

	if(NULL == (vm->stack = _stack_new(vm, closure, NULL)))
		ERROR_RETURN_LOG(int, "Cannot create stack for the entry point frame");

	pss_bytecode_regid_t retreg = _exec(vm);

	if(ERROR_CODE(int) == pss_value_decref(closure_value))
		ERROR_RETURN_LOG(int, "Cannot diepose the entry point closure");

	if(ERROR_CODE(pss_bytecode_regid_t) == retreg || vm->error != PSS_VM_ERROR_NONE)
		ERROR_RETURN_LOG(int, "The virtual machine is in an error state");

	int ret = 0;

	if(retbuf != NULL)
	{
		*retbuf = pss_frame_reg_get(vm->stack->frame, retreg);
		if(PSS_VALUE_KIND_ERROR == retbuf->kind) ret = ERROR_CODE(int);
		else if(ERROR_CODE(int) == pss_value_incref(*retbuf))
			ret = ERROR_CODE(int);
	}
	if(ERROR_CODE(int) == _stack_free(vm->stack))
		ret = ERROR_CODE(int);
	vm->stack = NULL;

	return ret;
}

pss_vm_exception_t* pss_vm_last_exception(pss_vm_t* vm)
{
	if(vm == NULL) ERROR_PTR_RETURN_LOG("Invalid arguments");
	if(vm->error == PSS_VM_ERROR_NONE) return NULL;

	pss_vm_exception_t* ret = (pss_vm_exception_t*)calloc(1, sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the exception");

	ret->message = _errstr[ret->code = vm->error];

	vm->error = PSS_VM_ERROR_NONE;

	_stack_t* ptr;
	for(ptr = vm->stack; NULL != ptr; )
	{
		_stack_t* cur = ptr;
		ptr = ptr->next;
		pss_vm_backtrace_t* new;

		if(NULL == (new = (pss_vm_backtrace_t*)malloc(sizeof(*ret->backtrace))))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the stack backtrace");

		new->line = cur->line;
		new->func = cur->func;
		new->next = ret->backtrace;
		ret->backtrace = new;

		if(ERROR_CODE(int) == _stack_free(cur))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate dispose the used stack frame");
	}

	vm->stack = NULL;

	return ret;
ERR:
	if(NULL != ret)
	{
		pss_vm_backtrace_t* ptr;
		for(ptr = ret->backtrace; NULL != ptr;)
		{
			pss_vm_backtrace_t* this = ptr;
			ptr = ptr->next;
			free(this);
		}
		free(ret);
	}

	return NULL;
}

int pss_vm_exception_free(pss_vm_exception_t* exception)
{
	if(NULL == exception) ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_vm_backtrace_t* ptr;
	for(ptr = exception->backtrace; NULL != ptr;)
	{
		pss_vm_backtrace_t* this = ptr;
		ptr = ptr->next;
		free(this);
	}

	free(exception);

	return 0;
}
