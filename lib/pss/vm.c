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

#if 0
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



}

pss_global_t* pss_global_new()
{
	pss_global_t* ret = (pss_global_t*)malloc(sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the PSS Global");

	return ret;
}


#endif
