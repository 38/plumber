/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the Plumber Service Script runtime vm
 * @file lang/vm.h
 **/
#ifndef __PLUMBER_LANG_VM_H__
#define __PLUMBER_LANG_VM_H__

/**
 * @brief the incomplete type for a vm
 **/
typedef struct _lang_vm_t lang_vm_t;

/**
 * @brief the typecode for a runtime value
 **/
typedef enum {
	LANG_VM_VALUE_TYPE_UNDEFINED,/*!< an uninitialized variable */
	LANG_VM_VALUE_TYPE_NUM,      /*!< a number */
	LANG_VM_VALUE_TYPE_SERVLET,  /*!< a servlet id */
	LANG_VM_VALUE_TYPE_STRID,    /*!< a string ID */
	LANG_VM_VALUE_TYPE_SERVICE,  /*!< a service graph */
	LANG_VM_VALUE_TYPE_RT_STR = LANG_VM_VALUE_TYPE_STRID | 0x100, /*!< indicates this is a runtime string */
	LANG_VM_VALUE_TYPE_MASK_TYPE_CODE = 0xff, /*!< the mask for actual type */
	LANG_VM_VALUE_TYPE_FLAG_RT = 0x100        /*!< indicates this is a dynamic string */
} lang_vm_value_type_t;

/**
 * @brief represents a service graph
 **/
typedef struct _lang_vm_service_t lang_vm_service_t;

/**
 * @brief represents a servlet node in a service graph
 **/
typedef struct {
	runtime_stab_entry_t servlet;   /*!< the servlet id */
	sched_service_node_id_t node;   /*!< the node id */
} lang_vm_servlet_t;

/**
 * @brief represents a runtime value
 **/
typedef struct {
	lang_vm_value_type_t type;   /*!< the typecode for this value */
	union {
		uint32_t strid;             /*!< a string id */
		int32_t num;                /*!< a number */
		char* str;                  /*!< a runtime string */
		lang_vm_servlet_t servlet;  /*!< describe a servlet */
		lang_vm_service_t* service; /*!< a service */
	};
} lang_vm_value_t;

/**
 * @brief create a new vm instance
 * @param code the bytecode table contains the code
 * @return the newly created vm instance, NULL on error
 **/
lang_vm_t* lang_vm_new(lang_bytecode_table_t* code);

/**
 * @brief free a used vm instance
 * @param vm the target vm
 * @note this function do not free the bytecode table attached to the vm
 * @return the status code
 **/
int lang_vm_free(lang_vm_t* vm);

/**
 * @brief execute the bytecode in the bytecode table
 * @param vm the target vm
 * @return status code
 **/
int lang_vm_exec(lang_vm_t* vm);

/**
 * @brief peek a symbol in the mv
 * @param vm the target vm
 * @param symbol the symbol id to peek
 * @note the symbol is actually a list of string which ends with NULL. for example module.tcp.port
 *       is represented as {"module", "tcp", "port", NULL}
 * @return the pointer to the target value, NULL on error
 **/
lang_vm_value_t* lang_vm_peek(const lang_vm_t* vm, const char* symbol);

/**
 * @brief get the  scheduler service from a vm runtime service value
 * @param service the vm runtime service typed value
 * @return the sched_service_t result, NULL on error
 **/
const sched_service_t* lang_vm_service_get_sched_service(const lang_vm_service_t* service);

#endif /* __PLUMBER_LANG_VM_H__ */
