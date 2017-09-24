/*
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
	[PSS_VM_ERROR_NONE]       = "Success",
	[PSS_VM_ERROR_BYTECODE]   = "Invalid bytecode",
	[PSS_VM_ERROR_TYPE]       = "Type error",
	[PSS_VM_ERROR_INTERNAL]   = "Interpreter interal error",
	[PSS_VM_ERROR_ARITHMETIC] = "Arithmetic error",
	[PSS_VM_ERROR_STACK]      = "Stack overflow",
	[PSS_VM_ERROR_ARGUMENT]   = "Argutment error",
	[PSS_VM_ERROR_MODULE]     = "Module cannot be loaded",
	[PSS_VM_ERROR_IMPORT]     = "Import error",
	[PSS_VM_ERROR_FAILED]     = "Failed to compelete requested operation",
	[PSS_VM_ERROR_ADD_NODE]   = "Failed to add servlet node",
	[PSS_VM_ERROR_PIPE]       = "Failed to add pipe between servlets",
	[PSS_VM_ERROR_SERVICE]    = "Cannot start the service",
	[PSS_VM_ERROR_UNDEF]      = "Invalid operation on undefined value",
	[PSS_VM_ERROR_NONFUNC]    = "Invalid invocation to non-function value"
};

/**
 * @brief Represents one frame on the stack
 **/
typedef struct _stack_t {
	pss_bytecode_regid_t          arg[PSS_VM_ARG_MAX];   /*!< The argument register list */
	uint32_t                      argc;   /*!< The argument counter */
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
	uint32_t       killed;      /*!< If this VM gets killed */
	_stack_t*      stack;       /*!< The stack we are using */
	pss_dict_t*    global;      /*!< The global variable table */
	pss_vm_external_global_ops_t external_global_hook;  /*!< The external global hook */
};

/**
 * @brief Create a new stack frame
 * @param host The host VM
 * @param closure The colsure to run
 * @param parent The parent stack
 * @return The newly created stack
 **/
static inline _stack_t* _stack_new(pss_vm_t* host, const pss_closure_t* closure, _stack_t* parent)
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
	ret->func = "<Anonymous>";
	ret->next = NULL;
	ret->argc = 0;

	pss_bytecode_regid_t const* arg_regs;
	int argc = pss_bytecode_segment_get_args(ret->code, &arg_regs);
	if(ERROR_CODE(int) == argc)
	    ERROR_LOG_GOTO(ERR, "Cannot get the number of arguments of the code segment");

	int actual_size = parent == NULL ? 0 : (int)parent->argc;

	if(NULL != parent) parent->argc = 0;

	if(NULL == (ret->frame = pss_closure_get_frame(closure)))
	    ERROR_LOG_GOTO(ERR, "Cannot duplicate the environment frame");

	if(argc > actual_size) argc = actual_size;

	int i;
	for(i = 0; i < argc; i ++)
	{
		pss_value_t value = pss_frame_reg_get(parent->frame, parent->arg[i]);

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
		if(raise) vm->error = (value.kind != PSS_VALUE_KIND_UNDEF ? PSS_VM_ERROR_TYPE : PSS_VM_ERROR_UNDEF);
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
		if(raise) vm->error = type == PSS_VALUE_REF_TYPE_CLOSURE ? PSS_VM_ERROR_NONFUNC : PSS_VM_ERROR_TYPE;
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
 * @brief Handles new-dict, new-closure
 * @param vm The virtual machine
 * @param inst The instruction
 * @return status code, which indicates if we have an internal error.
 *         If the error is about the code not the virtual machine, it should
 *         return 0, and set the virtual machine's error code
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

/**
 * Handles load-int, load-str
 **/
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
			break;
		}
		case PSS_BYTECODE_RTYPE_UNDEF:
		    break;
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

/**
 * Handles get-val, set-val, get-key
 **/
static inline int _exec_dict(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	uint32_t dict_idx, key_idx, reg_idx;
	switch(inst->opcode)
	{
		case PSS_BYTECODE_OPCODE_GET_VAL:
		case PSS_BYTECODE_OPCODE_GET_KEY:
		    dict_idx = 0, key_idx = 1, reg_idx = 2;
		    break;
		case PSS_BYTECODE_OPCODE_SET_VAL:
		    reg_idx = 0, dict_idx = 1, key_idx = 2;
		    break;
		default:
		    vm->error = PSS_VM_ERROR_BYTECODE;
		    return 0;
	}
	pss_dict_t* dict = (pss_dict_t*)_value_get_ref_data(vm, _read_reg(vm, inst, dict_idx), PSS_VALUE_REF_TYPE_DICT, 1);
	if(NULL == dict) return 0;
	pss_value_t keyval = _read_reg(vm, inst, key_idx);

	if(inst->opcode == PSS_BYTECODE_OPCODE_GET_KEY)
	{
		if(!_is_value_kind(vm, keyval, PSS_VALUE_KIND_NUM, 1))
		    return 0;

		const char* res_str = pss_dict_get_key(dict, (uint32_t)keyval.num);
		if(NULL == res_str)
		    ERROR_RETURN_LOG(int, "Cannot read the key");

		char* runtime_str = pss_string_concat(res_str, "");
		if(NULL == runtime_str)
		    ERROR_RETURN_LOG(int, "Cannot create the runtime string");

		pss_value_t result = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, runtime_str);
		if(result.kind == PSS_VALUE_KIND_ERROR)
		{
			free(runtime_str);
			ERROR_RETURN_LOG(int, "Cannot careate new value for the result string");
		}

		if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[2], result))
		{
			pss_value_decref(result);
			ERROR_RETURN_LOG(int, "Cannot put the result string to stack frame");
		}

		return 0;
	}

	char buf[4096];
	const char* key = _get_string_repr(vm, keyval, buf, sizeof(buf));
	if(NULL == key) ERROR_RETURN_LOG(int, "Cannot convert the key to string");

	if(inst->opcode == PSS_BYTECODE_OPCODE_SET_VAL)
	{
		pss_value_t value = _read_reg(vm, inst, reg_idx);
		if(value.kind == PSS_VALUE_KIND_ERROR)
		    ERROR_RETURN_LOG(int, "Cannot read the source register");

		if(ERROR_CODE(int) == pss_dict_set(dict, key, value))
		    ERROR_RETURN_LOG(int, "Cannot insert the value to the dictionary");
	}
	else
	{
		pss_value_t value = pss_dict_get(dict, key);
		if(value.kind == PSS_VALUE_KIND_ERROR)
		    ERROR_RETURN_LOG(int, "Cannot read the dictionary");

		if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[reg_idx], value))
		    ERROR_RETURN_LOG(int, "Cannot write the value to the reigster frame");
	}

	return 0;
}

/**
 * Handles sub, mul, div, mod, and, or, xor
 **/
static inline int _exec_arithmetic_logic(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t left = _read_reg(vm, inst, 0);
	pss_value_t right = _read_reg(vm, inst, 1);
	pss_value_t result = {};

	if(inst->opcode == PSS_BYTECODE_OPCODE_AND ||
	   inst->opcode == PSS_BYTECODE_OPCODE_OR  ||
	   inst->opcode == PSS_BYTECODE_OPCODE_XOR)
	{
		if(left.kind == PSS_VALUE_KIND_UNDEF) left.kind = PSS_VALUE_KIND_NUM, left.num = 0;
		if(right.kind == PSS_VALUE_KIND_UNDEF) right.kind = PSS_VALUE_KIND_NUM, right.num = 0;
	}

	if(_is_value_kind(vm, left, PSS_VALUE_KIND_NUM, 1) &&
	   _is_value_kind(vm, right, PSS_VALUE_KIND_NUM, 1))
	{
		result.kind = PSS_VALUE_KIND_NUM;
		switch(inst->opcode)
		{
			case PSS_BYTECODE_OPCODE_SUB:
			    result.num = left.num - right.num;
			    break;
			case PSS_BYTECODE_OPCODE_MUL:
			    result.num = left.num * right.num;
			    break;
			case PSS_BYTECODE_OPCODE_DIV:
			case PSS_BYTECODE_OPCODE_MOD:
			    if(right.num == 0)
			    {
				    vm->error = PSS_VM_ERROR_ARITHMETIC;
				    return 0;
			    }
			    if(inst->opcode == PSS_BYTECODE_OPCODE_MOD)
			        result.num = left.num % right.num;
			    else
			        result.num = left.num / right.num;
			    break;
			case PSS_BYTECODE_OPCODE_AND:
			    result.num = left.num && right.num;
			    break;
			case PSS_BYTECODE_OPCODE_OR:
			    result.num = left.num || right.num;
			    break;
			case PSS_BYTECODE_OPCODE_XOR:
			    result.num = left.num ^ right.num;
			    break;
			default:
			    vm->error = PSS_VM_ERROR_BYTECODE;
			    return 0;
		}
	}

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[2], result))
	    ERROR_RETURN_LOG(int, "Cannot put the value to the register frame");

	return 0;
}
/**
 * @brief Handles generic operators: add, eq, le, lt
 * @note For the undefined type, the only thing we allows is equal check, because otherwise it doesn't make sense
 **/
static inline int _exec_generic(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t left = _read_reg(vm, inst, 0);
	pss_value_t right = _read_reg(vm, inst, 1);
	pss_value_t result = {};

	int lundef = _is_value_kind(vm, left, PSS_VALUE_KIND_UNDEF, 0);
	int rundef = _is_value_kind(vm, right, PSS_VALUE_KIND_UNDEF, 0);

	if(lundef || rundef)
	{
		if(inst->opcode == PSS_BYTECODE_OPCODE_EQ)
		    result.num = (lundef && rundef);
		else if(inst->opcode == PSS_BYTECODE_OPCODE_NE)
		    result.num = !(lundef && rundef);
		else
		{
			vm->error = PSS_VM_ERROR_UNDEF;
			return 0;
		}
	}

	if(_is_value_kind(vm, left, PSS_VALUE_KIND_NUM, 0) &&
	   _is_value_kind(vm, right, PSS_VALUE_KIND_NUM, 0))
	{
		result.kind = PSS_VALUE_KIND_NUM;
		switch(inst->opcode)
		{
			case PSS_BYTECODE_OPCODE_ADD:
			    result.num = left.num + right.num;
			    break;
			case PSS_BYTECODE_OPCODE_EQ:
			    result.num = (left.num == right.num);
			    break;
			case PSS_BYTECODE_OPCODE_NE:
			    result.num = (left.num != right.num);
			    break;
			case PSS_BYTECODE_OPCODE_LT:
			    result.num = (left.num < right.num);
			    break;
			case PSS_BYTECODE_OPCODE_LE:
			    result.num = (left.num <= right.num);
			    break;
			case PSS_BYTECODE_OPCODE_GE:
			    result.num = (left.num >= right.num);
			    break;
			case PSS_BYTECODE_OPCODE_GT:
			    result.num = (left.num > right.num);
			    break;
			default:
			    vm->error = PSS_VM_ERROR_BYTECODE;
			    return 0;
		}
	}
	else
	{
		char leftbuf[4096];
		char rightbuf[4096];
		const char* left_str = _get_string_repr(vm, left, leftbuf, sizeof(leftbuf));
		if(NULL == left_str) ERROR_RETURN_LOG(int, "Cannot convert the left operand to string");

		const char* right_str = _get_string_repr(vm, right, rightbuf, sizeof(rightbuf));
		if(NULL == right_str) ERROR_RETURN_LOG(int, "Cannot convert the right operand to string");

		switch(inst->opcode)
		{
			case PSS_BYTECODE_OPCODE_ADD:
			{
				char* result_str = pss_string_concat(left_str, right_str);
				if(NULL == result_str)
				    ERROR_RETURN_LOG(int, "Cannot concate two string");

				result = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, result_str);
				if(result.kind == PSS_VALUE_KIND_ERROR)
				{
					free(result_str);
					ERROR_RETURN_LOG(int, "Cannot create string value");
				}
				break;
			}
			case PSS_BYTECODE_OPCODE_LE:
			case PSS_BYTECODE_OPCODE_LT:
			case PSS_BYTECODE_OPCODE_EQ:
			case PSS_BYTECODE_OPCODE_GE:
			case PSS_BYTECODE_OPCODE_GT:
			case PSS_BYTECODE_OPCODE_NE:
			{
				int cmpres = strcmp(left_str, right_str);

				result.kind = PSS_VALUE_KIND_NUM;

				if(inst->opcode == PSS_BYTECODE_OPCODE_LE)
				    result.num = (cmpres <= 0);
				else if(inst->opcode == PSS_BYTECODE_OPCODE_LT)
				    result.num = (cmpres < 0);
				else if(inst->opcode == PSS_BYTECODE_OPCODE_EQ)
				    result.num = (cmpres == 0);
				else if(inst->opcode == PSS_BYTECODE_OPCODE_GE)
				    result.num = (cmpres >= 0);
				else if(inst->opcode == PSS_BYTECODE_OPCODE_GT)
				    result.num = (cmpres > 0);
				else if(inst->opcode == PSS_BYTECODE_OPCODE_NE)
				    result.num = (cmpres != 0);
				break;
			}
			default:
			    vm->error = PSS_VM_ERROR_BYTECODE;
			    return 0;
		}
	}

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[2], result))
	{
		pss_value_decref(result);
		ERROR_RETURN_LOG(int, "Cannot set the register value");
	}

	return 0;
}

/**
 * Handles the jump instruction: jz, jump
 **/
static inline int _exec_jump(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t cond = {
		.kind = PSS_VALUE_KIND_NUM,
		.num  = 0
	};
	pss_value_t target = _read_reg(vm, inst, inst->info->num_regs - 1u);
	if(!_is_value_kind(vm, target, PSS_VALUE_KIND_NUM, 1))
	    return 0;

	if(inst->opcode == PSS_BYTECODE_OPCODE_JZ)
	{
		cond = _read_reg(vm, inst, 0);
		if(!_is_value_kind(vm, cond, PSS_VALUE_KIND_NUM, 1))
		    return 0;
	}

	if(cond.num == 0)
	    vm->stack->ip = (pss_bytecode_addr_t)(target.num - 1);

	return 0;
}

/**
 * @brief Handles the global access instructions: global-get, global-set
 * @note  In this function, we actuall check the external globals before we move on to the next
 **/
static inline int _exec_global(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	uint32_t key_idx, reg_idx;
	if(inst->opcode == PSS_BYTECODE_OPCODE_GLOBAL_GET)
	    key_idx = 0, reg_idx = 1;
	else if(inst->opcode == PSS_BYTECODE_OPCODE_GLOBAL_SET)
	    key_idx = 1, reg_idx = 0;
	else
	{
		vm->error = PSS_VM_ERROR_BYTECODE;
		return 0;
	}
	const char* key = _value_get_ref_data(vm, _read_reg(vm, inst, key_idx), PSS_VALUE_REF_TYPE_STRING, 1);
	if(NULL == key) return 0;

	if(inst->opcode == PSS_BYTECODE_OPCODE_GLOBAL_GET)
	{
		pss_value_t value = {};
		/* Try the external global hook if defined */
		if(vm->external_global_hook.get != NULL)
		    value = vm->external_global_hook.get(key);

		/* If we can not find anything with the global hook */
		if(value.kind == PSS_VALUE_KIND_UNDEF)
		    value = pss_dict_get(vm->global, key);

		if(PSS_VALUE_KIND_ERROR == value.kind)
		    ERROR_RETURN_LOG(int, "Cannot read the global dictionary");

		if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[reg_idx], value))
		    ERROR_RETURN_LOG(int, "Cannot write the value to register");
	}
	else
	{
		pss_value_t value = pss_frame_reg_get(vm->stack->frame, inst->reg[reg_idx]);

		if(PSS_VALUE_KIND_ERROR == value.kind)
		    ERROR_RETURN_LOG(int, "Cannot read the value of the register");

		int rc = vm->external_global_hook.set != NULL ? vm->external_global_hook.set(key, value) : 0;

		if(ERROR_CODE(int) == rc) ERROR_RETURN_LOG(int, "The external global setter returns an error code");

		if(rc == 0 && ERROR_CODE(int) == pss_dict_set(vm->global, key, value))
		    ERROR_RETURN_LOG(int, "Cannot write the value to the global dicitonary");
	}

	return 0;
}

/**
 * Handles len
 **/
static inline int _exec_len(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t value = _read_reg(vm, inst, 0);
	pss_value_t result = {
		.kind = PSS_VALUE_KIND_NUM
	};
	const char* str = (const char*)_value_get_ref_data(vm, value, PSS_VALUE_REF_TYPE_STRING, 0);
	pss_dict_t* dict;

	if(NULL != str)
	    result.num = (pss_bytecode_numeric_t)strlen(str);
	else if(NULL != (dict = (pss_dict_t*)_value_get_ref_data(vm, value, PSS_VALUE_REF_TYPE_DICT, 0)))
	    result.num = pss_dict_size(dict);
	else
	{
		vm->error = PSS_VM_ERROR_TYPE;
		return 0;
	}

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[1], result))
	    ERROR_RETURN_LOG(int, "Cannot write the result to register frame");

	return 0;
}

/**
 * Handles a call instruction on a builtin
 **/
static inline int _exec_builtin(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t func = _read_reg(vm, inst, 0);
	if(!_is_value_kind(vm, func, PSS_VALUE_KIND_BUILTIN, 1)) return 0;

	uint32_t argc = vm->stack->argc;
	vm->stack->argc = 0;

	pss_value_t argv[argc];
	memset(argv, 0, sizeof(argv[0]) * argc);

	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		argv[i] = pss_frame_reg_get(vm->stack->frame, vm->stack->arg[i]);
		if(argv[i].kind == PSS_VALUE_KIND_ERROR)
		    ERROR_RETURN_LOG(int, "Cannot get the value from the argument list");
	}


	pss_value_t result = func.builtin(vm, argc, argv);
	if(result.kind == PSS_VALUE_KIND_ERROR)
	{
		vm->error = (pss_vm_error_t)result.num;
		LOG_ERROR("The builtin function returns an error");
		return 0;
	}

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[1], result))
	    ERROR_RETURN_LOG(int, "Cannot set the result value to the register frame");

	return 0;
}

static inline pss_bytecode_regid_t _exec(pss_vm_t* vm);

/**
 * Handle the call instruction
 **/
static inline int _exec_call(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t func = _read_reg(vm, inst, 0);

	if(_is_value_kind(vm, func, PSS_VALUE_KIND_BUILTIN, 0))
	    return _exec_builtin(vm, inst);

	const pss_closure_t* closure = (const pss_closure_t*)_value_get_ref_data(vm, func, PSS_VALUE_REF_TYPE_CLOSURE, 1);
	if(NULL == closure) return 0;

	_stack_t* this_stack = vm->stack;

	_stack_t* new_stack = _stack_new(vm, closure, vm->stack);
	if(NULL == new_stack) ERROR_RETURN_LOG(int, "Cannot create new stack frame");

	new_stack->next = this_stack;
	vm->stack = new_stack;

	pss_bytecode_regid_t ret;

	if(ERROR_CODE(pss_bytecode_regid_t) == (ret = _exec(vm)))
	    return 0;

	pss_value_t retval = pss_frame_reg_get(vm->stack->frame, ret);
	if(retval.kind == PSS_VALUE_KIND_ERROR)
	    ERROR_RETURN_LOG(int, "Cannot fetch the return value of the last function call");

	if(ERROR_CODE(int) == pss_frame_reg_set(this_stack->frame, inst->reg[1], retval))
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

/**
 * Handle move instruction
 **/
static inline int _exec_move(pss_vm_t* vm, const pss_bytecode_instruction_t* inst)
{
	pss_value_t value = _read_reg(vm, inst, 0);

	if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, inst->reg[1], value))
	    ERROR_RETURN_LOG(int, "Cannot change the value of target register");

	return 0;
}

/**
 * @brief Run the code that has been loaded in the VM until current function exited
 * @param vm The virtual machine
 * @note  This function assumes the stack has been set up already
 * @return The register ID that contains the result
 **/
static inline pss_bytecode_regid_t _exec(pss_vm_t* vm)
{
	pss_bytecode_regid_t retreg = ERROR_CODE(pss_bytecode_regid_t);
	_stack_t* top = vm->stack;
	vm->level ++;

	if(vm->level > PSS_VM_STACK_LIMIT) vm->error = PSS_VM_ERROR_STACK;

	while(vm->stack != NULL && !vm->killed && PSS_VM_ERROR_NONE == vm->error && retreg == ERROR_CODE(pss_bytecode_regid_t))
	{
		pss_bytecode_instruction_t inst;
		if(ERROR_CODE(int) == pss_bytecode_segment_get_inst(top->code, top->ip, &inst))
		{
			LOG_ERROR("Cannot fetch instruction at address 0x%x", top->ip);
			vm->error = PSS_VM_ERROR_INTERNAL;
			break;
		}
#ifdef LOG_DEBUG_ENABLED
		static char instbuf[128];
		if(NULL == (pss_bytecode_segment_inst_str(top->code, top->ip, instbuf, sizeof(instbuf))))
		    LOG_WARNING("Cannot print current instruction");
		else
		    LOG_DEBUG("Current Instruction: <%p:0x%.8x> %s", top->code, top->ip, instbuf);
#endif

		int rc = 0;
		switch(inst.info->operation)
		{
			case PSS_BYTECODE_OP_NEW:
			    rc = _exec_new(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_LOAD:
			    rc = _exec_load(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_LEN:
			    rc = _exec_len(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_SETVAL:
			case PSS_BYTECODE_OP_GETVAL:
			case PSS_BYTECODE_OP_GETKEY:
			    rc = _exec_dict(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_CALL:
			    rc = _exec_call(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_JUMP:
			case PSS_BYTECODE_OP_JZ:
			    rc = _exec_jump(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_LT:
			case PSS_BYTECODE_OP_LE:
			case PSS_BYTECODE_OP_EQ:
			case PSS_BYTECODE_OP_NE:
			case PSS_BYTECODE_OP_GE:
			case PSS_BYTECODE_OP_GT:
			case PSS_BYTECODE_OP_ADD:
			    rc = _exec_generic(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_SUB:
			case PSS_BYTECODE_OP_MUL:
			case PSS_BYTECODE_OP_DIV:
			case PSS_BYTECODE_OP_MOD:
			case PSS_BYTECODE_OP_AND:
			case PSS_BYTECODE_OP_OR:
			case PSS_BYTECODE_OP_XOR:
			    rc = _exec_arithmetic_logic(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_MOVE:
			    rc = _exec_move(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_GLOBALGET:
			case PSS_BYTECODE_OP_GLOBALSET:
			    rc = _exec_global(vm, &inst);
			    break;
			case PSS_BYTECODE_OP_RETURN:
			    retreg = inst.reg[0];
			    break;
			case PSS_BYTECODE_OP_ARG:
			    top->arg[top->argc ++] = inst.reg[0];
			    break;
			case PSS_BYTECODE_OP_DEBUGINFO:
			    if(inst.info->rtype == PSS_BYTECODE_RTYPE_INT)
			        top->line = (uint32_t)inst.num;
			    else if(inst.info->rtype == PSS_BYTECODE_RTYPE_STR)
			        top->func = inst.str;
			    else vm->error = PSS_VM_ERROR_BYTECODE;
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

	if(vm->killed && retreg == ERROR_CODE(pss_bytecode_regid_t))
	{
		pss_value_t undef = {};
		if(ERROR_CODE(int) == pss_frame_reg_set(vm->stack->frame, 0, undef))
			LOG_ERROR("Cannot set the stack frame");
		else
			retreg = 0;
	}

	if(vm->level == 0) vm->killed = 0;

	return retreg;
}

int pss_vm_kill(pss_vm_t* vm)
{
	if(vm == NULL) ERROR_RETURN_LOG(int, "Invalid arguments");
	vm->killed = 1;
	return 0;
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

	_stack_t* this_stack;

	if(NULL == (this_stack = _stack_new(vm, closure, NULL)))
	    ERROR_RETURN_LOG(int, "Cannot create stack for the entry point frame");

	this_stack->next = vm->stack;
	vm->stack = this_stack;

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

	vm->stack = vm->stack->next;
	if(ERROR_CODE(int) == _stack_free(this_stack))
	    ret = ERROR_CODE(int);



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

int pss_vm_set_external_global_callback(pss_vm_t* vm, pss_vm_external_global_ops_t ops)
{
	if(NULL == vm) ERROR_RETURN_LOG(int, "Invalid arguments");

	vm->external_global_hook = ops;

	return 0;
}

int pss_vm_add_builtin_func(pss_vm_t* vm, const char* name, pss_value_builtin_t func)
{
	if(NULL == vm || NULL == name || NULL == func)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_value_t value = {
		.kind = PSS_VALUE_KIND_BUILTIN,
		.builtin = func
	};

	if(ERROR_CODE(int) == pss_dict_set(vm->global, name, value))
	    ERROR_RETURN_LOG(int, "Cannot insert the builtin function to the global value");

	return 0;
}

int pss_vm_set_global(pss_vm_t* vm, const char* var, pss_value_t val)
{
	if(NULL == vm || NULL == var || val.kind == PSS_VALUE_KIND_ERROR)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == pss_dict_set(vm->global, var, val))
	    ERROR_RETURN_LOG(int, "Cannot write to the global dictionary");

	return 0;
}
