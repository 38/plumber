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

	int dict_size = (int)pss_dict_size(args);
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

#if 0
pss_global_t* pss_global_new()
{
	pss_global_t* ret = (pss_global_t*)malloc(sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the PSS Global");

	return ret;
}


#endif
