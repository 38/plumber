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
#include <pss/vm.h>
#include <pss/closure.h>

/**
 * @brief Represents one frame on the stack
 **/
typedef struct _stack_t {
	pss_vm_t*                     host;   /*! The host VM */
	const pss_bytecode_segment_t* code;   /*!< The code segment that is currently running */
	pss_frame_t*                  frame;  /*!< The register frame for current stack frame */
	pss_bytecode_addr_t           ip;     /*!< The instruction pointer - The address to the next bytecode to execute */
	uint32_t                      line;   /*!< Current line number */
	char*                         exmsg;  /*!< The exception message */
	const char*                   func;   /*!< Current function name */
	struct _stack_t*              next;   /*!< The next frame in the stack */
} _stack_t;

/**
 * @brief The actual data structure for a PSS Virtual Machine
 **/
struct _pss_vm_t {
	uint32_t       exception:1; /*!< If this VM is encountering exception */
	_stack_t*      stack;       /*!< The stack we are using */
	pss_dict_t*    global;      /*!< The global variable table */
};

static inline pss_value_t _strip_const(void* ptr)
{
	return *(pss_value_t*)ptr;
}

/**
 * @brief Create a new stack frame
 * @param host The host VM
 * @param env The environment
 * @param seg The code segment
 * @param args The argument dictionary
 * @return The newly created stack
 **/
static inline _stack_t* _stack_new(pss_vm_t* host, const pss_frame_t* env, const pss_bytecode_segment_t* seg, const pss_dict_t* args)
{
	_stack_t* ret = (_stack_t*)calloc(1, sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the stack frame");

	ret->host = host;
	ret->code = seg;
	ret->ip = 0;
	ret->line = 0;
	ret->exmsg = NULL;
	ret->func = NULL;
	ret->next = NULL;

	pss_bytecode_regid_t const* arg_regs;
	int argc = pss_bytecode_segment_get_args(seg, &arg_regs);
	if(ERROR_CODE(int) == argc)
		ERROR_LOG_GOTO(ERR, "Cannot get the number of arguments of the code segment");

	int dict_size = args == NULL ? 0 : (int)pss_dict_size(args);
	if(ERROR_CODE(int) == dict_size)
		ERROR_LOG_GOTO(ERR, "Cannot get the size of the argument list");

	if(NULL == (ret->frame = pss_frame_new(env)))
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

		if(ERROR_CODE(int) == pss_frame_reg_set(ret->frame, arg_regs[i], _strip_const(&value)))
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

static inline int _stack_free(_stack_t* stack)
{
	int rc = 0;
	if(NULL != stack->exmsg) 
		free(stack->exmsg);
	
	if(NULL != stack->frame && ERROR_CODE(int) == pss_frame_free(stack->frame))
	{
		LOG_ERROR("Cannot dispose the register frame");
		rc = ERROR_CODE(int);
	}

	free(stack);
	return rc;
}

static inline int _exec(pss_vm_t* vm)
{
#if 0
	while(vm->stack != NULL && !vm->exception)
	{
		_stack_t* top= vm->stack;
		pss_bytecode_instruction_t inst;
		if(ERROR_CODE(int) == pss_bytecode_segment_get_inst(top->code, top->ip, &inst))
		{
			LOG_ERROR("Cannot fetch instruction at address 0x%x", top->ip);
			vm->exception = 1u; 
			break;
		}

		int rc = 0;
		switch(inst->info->operation)
		{
			case PSS_BYTECODE_OP_NEW:
				rc = _exec_new(vm, &inst);
				break;
			default:
				rc = ERROR_CODE(int);
				LOG_ERROR("Invalid opration code %u", inst->info->operation);
		}
	}

	if(vm->exception)
		ERROR_RETURN_LOG(int, "VM has been set to exception state");
#endif
	(void)vm;
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

int pss_vm_run_module(pss_vm_t* vm, const pss_bytecode_module_t* module)
{
	if(NULL == vm || NULL == module)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(vm->exception)
		ERROR_RETURN_LOG(int, "VM is in exception state");

	pss_bytecode_segid_t segid = pss_bytecode_module_get_entry_point(module);
	if(ERROR_CODE(pss_bytecode_segid_t) == segid)
		ERROR_RETURN_LOG(int, "Cannot get the entry point segment ID");

	const pss_bytecode_segment_t* seg = pss_bytecode_module_get_seg(module, segid);
	if(NULL == seg)
		ERROR_RETURN_LOG(int, "Cannot get the segment from the segment ID %u", segid);

	if(NULL == (vm->stack = _stack_new(vm, NULL, seg, NULL)))
		ERROR_RETURN_LOG(int, "Cannot create stack for the entry point frame");

	return _exec(vm);
}
