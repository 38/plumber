/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The Bytecode virtual machine for PSS
 * @file include/pss/vm.h
 **/
#ifndef __PSS_VM_H__
#define __PSS_VM_H__

/**
 * @brief The data type for the PSS Virtual Machine
 **/
typedef struct _pss_vm_t pss_vm_t;

/**
 * @brief The stack backtrace
 **/
typedef struct _pss_vm_bracktrace_t {
	uint32_t     line;   /*!< The line number */
	const char*  func;   /*!< The function name (include the file name) */
	struct _pss_vm_bracktrace_t* next;  /*!< The next element in the stack */
} pss_vm_backtrace_t;

/**
 * @brief Represent a VM runtime error information
 **/
typedef struct {
	pss_vm_backtrace_t* backtrace;  /*!< The stack backtrack */
	char*               message;    /*!< The message */
} pss_vm_exception_t;

/**
 * @brief Create a new PSS virtual machine
 * @return The newly created PSS virtual machine
 **/
pss_vm_t* pss_vm_new();

/**
 * @brief Dispose an used PSS virtual machine
 * @param vm The virtual machine to dispose
 * @return status code
 **/
int pss_vm_free(pss_vm_t* vm);

/**
 * @brief Run a bytecode module in the PSS virtual machine
 * @param vm The virtual machine used to run the code
 * @param module The module to run
 * @return stauts code
 **/
int pss_vm_run_module(pss_vm_t* vm, const pss_bytecode_module_t* module);

/**
 * @brief Get the last exception occured with the virtual machine
 * @param vm The virtual machine
 * @return The exception description
 **/
pss_vm_exception_t* pss_vm_last_exception(const pss_vm_t* vm); 

/**
 * @brief Dispose a exception description
 * @param exception The exception description
 * @return status code
 **/
int pss_vm_exception_free(pss_vm_exception_t* exception);

#endif
