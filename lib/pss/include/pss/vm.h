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
 * @brief The error code of the VM
 **/
typedef enum {
	PSS_VM_ERROR_NONE,       /*!< Everything is good */
	PSS_VM_ERROR_INTERNAL,   /*!< The internal error */
	PSS_VM_ERROR_BYTECODE,   /*!< An invalid bytecode */
	PSS_VM_ERROR_TYPE,       /*!< The instruction gets an unsupported type */
	PSS_VM_ERROR_ARITHMETIC, /*!< The arithmetic error */
	PSS_VM_ERROR_STACK       /*!< The stack overflow exception */
} pss_vm_error_t;

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
	pss_vm_error_t      code;       /*!!< The error code */
	const char*         message;    /*!< The message */
} pss_vm_exception_t;

/**
 * @brief The data structure used to represent an external data, this is used
 *        because we want the Plumber runtime also be able to handle some part of
 *        the global variables
 **/
typedef struct {
	enum {
		PSS_VM_EXTERNAL_DATA_ERR,    /*!< The error */
		PSS_VM_EXTERNAL_DATA_UNDEF,  /*!< The callback cannot handle this name */
		PSS_VM_EXTERNAL_DATA_NUM,    /*!< A numeric value */
		PSS_VM_EXTERNAL_DATA_STR     /*!< A string value */
	}  type;                      /*!< The type of this internal data */
	union {
		pss_bytecode_numeric_t num;  /*!< The numeric value */
		const char*            str;  /*!< The string value */
	};
} pss_vm_external_data_t;

/**
 * @brief The callback functions for the external globals
 **/
typedef struct {
	/**
	 * @brief The getter func
	 * @param name The name of the variable
	 * @return The get result
	 **/
	pss_vm_external_data_t (*get)(const char* name);
	/**
	 * @biref The setter func
	 * @param name The name of the variable
	 * @param data The data we want to put
	 * @return The number of field that has been wrrite, or error code
	 **/
	int (*set)(const char* name, pss_vm_external_data_t data);
} pss_vm_external_global_ops_t;

//TODO: implement the external global and builtin-table

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
 * @param retbuf The return buffer
 * @return stauts code
 **/
int pss_vm_run_module(pss_vm_t* vm, const pss_bytecode_module_t* module, pss_value_t* retbuf);

/**
 * @brief Get the last exception occured with the virtual machine
 * @param vm The virtual machine, after we did this, the VM well become a good state again
 * @return The exception description
 **/
pss_vm_exception_t* pss_vm_last_exception(pss_vm_t* vm);

/**
 * @brief Dispose a exception description
 * @param exception The exception description
 * @return status code
 **/
int pss_vm_exception_free(pss_vm_exception_t* exception);


#endif
