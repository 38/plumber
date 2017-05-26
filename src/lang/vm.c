/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <api.h>
#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/equeue.h>
#include <itc/eloop.h>
#include <itc/binary.h>
#include <itc/modtab.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>
#include <sched/service.h>
#include <sched/loop.h>
#include <lang/bytecode.h>
#include <lang/prop.h>
#include <lang/vm.h>
#include <utils/hashmap.h>
#include <utils/mempool/oneway.h>
#include <utils/static_assertion.h>
#include <utils/vector.h>
#include <utils/string.h>
#include <utils/log.h>
#include <error.h>

/**
 * @brief the internal vm value data structure
 **/
typedef struct _value_t {
	uint32_t         refcnt;   /*!< the reference counter */
	struct _value_t* prev;     /*!< the double linked list contains all the values */
	struct _value_t* next;     /*!< the double linked list's next  pointer */
	lang_vm_value_t  data;    /*!< the actual value */
} _value_t;

/**
 * @brief the actual data structure for a VM
 **/
struct _lang_vm_t {
	uint32_t               num_reg;    /*!< the number of register for this vm */
	lang_prop_callback_vector_t* cb_vec; /*!< the property callback vector */
	lang_bytecode_table_t* bc_table;   /*!< the bytecode table */
	hashmap_t*             env;        /*!< the environment table */
	vector_t*              params;     /*!< the function param list */
	_value_t*              values;     /*!< the value list used by this vm */
	uintptr_t              __padding__[0];
	_value_t*              reg[0];        /*!< the register table */
};

/**
 * @brief represents a servlet in the graphviz output
 **/
typedef struct _servlet_t{
	sched_service_node_id_t nid; /*!< the node ID for this servlet */
	runtime_stab_entry_t    sid; /*!< the servlet id */
	const char* name;     /*!< the name of the node */
	const char* args;     /*!< the servlet initialize arguments */
	const char* graphviz; /*!< the additional graphviz label */
	struct _servlet_t* next; /*!< the linked list pointer */
} _servlet_t;

/**
 * @brief represents a pipe between two different node
 **/
typedef struct _pipe_t {
	sched_service_pipe_descriptor_t pipe;   /*!< the pipe data */
	struct _pipe_t* next;
} _pipe_t;


/**
 * @brief data structure for a runtime value with service type
 **/
struct _lang_vm_service_t {
	int initialized;    /*!< indicates if this service value has already be converted from sched_service_buffer_t to sched_service_t */
	union {
		sched_service_t* service;   /*!< the actual service */
		sched_service_buffer_t* buffer; /*!< represents the service buffer */
	};
	sched_service_node_id_t input_node; /*!< the input node id */
	sched_service_node_id_t output_node; /*!< the output node id */
	runtime_api_pipe_id_t   input_pipe;
	runtime_api_pipe_id_t   output_pipe;
	_servlet_t* nodes;     /*!< the node list */
	_pipe_t*    pipes;     /*!< the pipe list */
};


lang_vm_t* lang_vm_new(lang_bytecode_table_t* code)
{
	if(NULL == code) ERROR_PTR_RETURN_LOG("Invalid arguments");

	uint32_t num_regs = lang_bytecode_table_get_num_regs(code);
	if(ERROR_CODE(uint32_t) == num_regs)
	    ERROR_PTR_RETURN_LOG("cannot get the number of registers from the bytecode table");

	lang_vm_t* ret = (lang_vm_t*)calloc(1, sizeof(lang_vm_t) + sizeof(_value_t*) * num_regs);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory");
	ret->bc_table = code;
	ret->num_reg = num_regs;

	if(NULL == (ret->env = hashmap_new(LANG_VM_ENV_HASH_SIZE, LANG_VM_ENV_POOL_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create environmen hashmap");

	if(NULL == (ret->params = vector_new(sizeof(uint32_t), LANG_VM_ENV_POOL_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create param vector");

	if(NULL == (ret->cb_vec = lang_prop_callback_vector_new(code)))
	    ERROR_LOG_GOTO(ERR, "Cannot create property callback vector");

	return ret;
ERR:
	if(NULL != ret->env) hashmap_free(ret->env);
	if(NULL != ret->params) vector_free(ret->params);
	if(NULL != ret->cb_vec) lang_prop_callback_vector_free(ret->cb_vec);
	free(ret);
	return NULL;
}

static inline void _incref(_value_t* value);
static inline int _decref(lang_vm_t* vm, _value_t* value);
static inline _value_t* _value_new(lang_vm_t* vm);

/**
 * @brief create a new runtime service object
 * @return the newly created runtime service object, or NULL on error
 **/
static inline lang_vm_service_t* _service_new()
{
	lang_vm_service_t* ret = (lang_vm_service_t*)calloc(1, sizeof(lang_vm_service_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the service object");

	ret->initialized = 0;
	if(NULL == (ret->buffer = sched_service_buffer_new()))
	    ERROR_LOG_GOTO(ERR, "Cannot create new scheduler service buffer for the service object");

	ret->input_node = ret->output_node = ERROR_CODE(sched_service_node_id_t);
	ret->input_pipe = ret->output_pipe = ERROR_CODE(runtime_api_pipe_id_t);

	return ret;
ERR:
	free(ret);
	return NULL;
}

/**
 * @brief read a value in the register
 * @param vm current VM
 * @param reg the register reference
 * @return the pointer to the value
 **/
static inline _value_t* _get_reg(lang_vm_t* vm, uint32_t reg)
{
	if(reg >= vm->num_reg) ERROR_PTR_RETURN_LOG("Invalid register number %u", reg);

	return vm->reg[reg];
}

/**
 * @brief get the string constant from the string id
 * @param vm current VM instance
 * @param val the string value
 * @return the result string or NULL on error
 **/
static inline const char* _get_string(const lang_vm_t* vm, const _value_t* val)
{
	if(val->data.type == LANG_VM_VALUE_TYPE_STRID)
	{
		lang_bytecode_operand_id_t id = {
			.type = LANG_BYTECODE_OPERAND_STR,
			.id = val->data.strid
		};
		const char* ret = lang_bytecode_table_str_id_to_string(vm->bc_table, id);
		return ret;
	}
	else return val->data.str;
}
/**
 * @brief get the length of the string representation of the value
 * @param vm the vm instance
 * @param val the value
 * @return the length or status code
 **/
static inline size_t _value_string_len(lang_vm_t* vm, _value_t* val)
{
	string_buffer_t sbuf;
	char buf[32];
	const char* str = NULL;
	string_buffer_open(buf, sizeof(buf), &sbuf);
	switch(val->data.type)
	{
		case LANG_VM_VALUE_TYPE_STRID:
		case LANG_VM_VALUE_TYPE_RT_STR:
		    str = _get_string(vm, val);
		    if(NULL == str) ERROR_RETURN_LOG(size_t, "Cannot get the string from VM runtime");
		    return strlen(str);
		case LANG_VM_VALUE_TYPE_SERVICE:
		    string_buffer_appendf(&sbuf, "<Service@%p>", val->data.service);
		    break;
		case LANG_VM_VALUE_TYPE_SERVLET:
		    string_buffer_appendf(&sbuf, "[Node %u]<Servlet %u>", val->data.servlet.node, val->data.servlet.servlet);
		    break;
		case LANG_VM_VALUE_TYPE_NUM:
		    string_buffer_appendf(&sbuf, "%d", val->data.num);
		    break;
		case LANG_VM_VALUE_TYPE_UNDEFINED:
		    string_buffer_appendf(&sbuf, "undefined");
		    break;
		default:
		    ERROR_RETURN_LOG(size_t, "Invalid runtime type");
	}

	str = string_buffer_close(&sbuf);
	return strlen(str);
}

/**
 * @brief convert the value to string and put it to string buffer
 * @param vm the VM instance
 * @param val the value to convert
 * @param sbuf the string buffer
 * @return the status code
 **/
static inline int _value_to_string(lang_vm_t* vm, _value_t* val, string_buffer_t* sbuf)
{
	const char* str;
	switch(val->data.type)
	{
		case LANG_VM_VALUE_TYPE_STRID:
		case LANG_VM_VALUE_TYPE_RT_STR:
		    str = _get_string(vm, val);
		    if(NULL == str) ERROR_RETURN_LOG(int, "Cannot get the string from VM runtime");
		    string_buffer_appendf(sbuf, "%s", str);
		    return 0;
		case LANG_VM_VALUE_TYPE_SERVICE:
		    string_buffer_appendf(sbuf, "<Service@%p>", val->data.service);
		    return 0;
		case LANG_VM_VALUE_TYPE_SERVLET:
		    string_buffer_appendf(sbuf, "[Node %u]<Servlet %u>", val->data.servlet.node, val->data.servlet.servlet);
		    return 0;
		case LANG_VM_VALUE_TYPE_NUM:
		    string_buffer_appendf(sbuf, "%d", val->data.num);
		    return 0;
		case LANG_VM_VALUE_TYPE_UNDEFINED:
		    string_buffer_append("undefined", sbuf);
		    return 0;
		default:
		    ERROR_RETURN_LOG(int, "Invalid runtime type");
	}
}

/**
 * @brief read a register of type T to variable name
 * @param T the expected value type
 * @param name the target variable name
 **/
#define _GET_PARAM(T, name) \
    const _value_t* name##_val = _get_reg(vm, name##_reg);\
    if(NULL == name##_val|| LANG_VM_VALUE_TYPE_##T != (name##_val->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE))\
        ERROR_RETURN_LOG(int, "Invalid "#name" value");

/**
 * @brief add a pipe to the service
 * @param vm the vm instance
 * @param service_reg the register contains the service object
 * @param src_node_reg the register that carries the source node id
 * @param src_pipe_reg the register that carries the source pipe name
 * @param dst_node_reg the register that carries the destination node id
 * @param dst_pipe_reg the register that carries the destination pipe name
 * @return status code
 **/
static inline int _service_add_pipe(lang_vm_t* vm,         uint32_t service_reg,
                                    uint32_t src_node_reg, uint32_t src_pipe_reg,
                                    uint32_t dst_node_reg, uint32_t dst_pipe_reg)
{

	_GET_PARAM(SERVICE, service);
	_GET_PARAM(SERVLET, src_node);
	_GET_PARAM(STRID, src_pipe);
	_GET_PARAM(SERVLET, dst_node);
	_GET_PARAM(STRID, dst_pipe);
	lang_vm_service_t* service = service_val->data.service;

	if(service->initialized)
	    ERROR_RETURN_LOG(int, "Cannot modify a service that has been already initialized");

	lang_vm_servlet_t src_servlet = src_node_val->data.servlet;
	lang_vm_servlet_t dst_servlet = dst_node_val->data.servlet;
	const char* src_pipe = _get_string(vm, src_pipe_val);
	if(NULL == src_pipe) ERROR_RETURN_LOG(int, "Cannot get the pipe name from the string table");
	const char* dst_pipe = _get_string(vm, dst_pipe_val);
	if(NULL == dst_pipe) ERROR_RETURN_LOG(int, "Cannot get the pipe name from the string table");

	sched_service_pipe_descriptor_t desc = {
		.source_node_id = src_servlet.node,
		.source_pipe_desc = runtime_stab_get_pipe(src_servlet.servlet, src_pipe),
		.destination_node_id = dst_servlet.node,
		.destination_pipe_desc = runtime_stab_get_pipe(dst_servlet.servlet, dst_pipe)
	};
	int rc = sched_service_buffer_add_pipe(service->buffer, desc);

	if(ERROR_CODE(int) == rc)
	    ERROR_RETURN_LOG(int, "Cannot add pipe to the service buffer");

	_pipe_t* new_pipe = (_pipe_t*)malloc(sizeof(_pipe_t));
	if(NULL == new_pipe) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate new node for the newly added pipe");

	new_pipe->pipe = desc;
	new_pipe->next = service->pipes;
	service->pipes = new_pipe;
	return 0;
}

/**
 * @brief dispose a parsed command line param
 * @param cmd the command line param to dispose
 * @return nothing
 **/
static inline void _free_cmdline(vector_t* cmd)
{
	size_t n = vector_length(cmd);
	size_t i;
	for(i = 0; i < n; i ++)
	{
		char* str = *VECTOR_GET_CONST(char *, cmd, i);
		if(NULL != str) free(str);
	}

	vector_free(cmd);
}

/**
 * @brief parse a command line param
 * @param cmdline the command line param text
 * @return the newly created vector contains all the sections for a command line
 **/
static inline vector_t* _parse_cmdline(const char* cmdline)
{
	vector_t* ret = vector_new(sizeof(char*), 4);  /* we usually do not have cmdline more than 4 args */
	if(NULL == ret) ERROR_PTR_RETURN_LOG("cannot create result vector");
	enum {
		_NORMAL,
		_ESCAPE
	} state = _NORMAL;
	/* strip leading white space */
	for(;*cmdline == ' ' || *cmdline == '\t'; cmdline ++);
	const char* begin = cmdline;
	/* parse the cmdline */
	do {
		char ch = *cmdline;
		if(ch == 0 || ((ch == ' ' || ch == '\t') && state == _NORMAL))
		{
			size_t sz = (size_t)(cmdline - begin);
			if(sz > 0)
			{
				char* start = NULL;
				char* mem = start = (char*)malloc(sz + 1);
				const char* ptr;
				int esc = 0;
				for(ptr = begin; ptr < cmdline; ptr ++)
				    if(!esc)
				    {
					    if(*ptr == '\\') esc = 1;
					    else *(mem++) = *ptr;
				    }
				    else
				    {
					    *(mem++) = *ptr;
					    esc = 0;
				    }
				*mem = 0;
				vector_t *new_vec = vector_append(ret, &start);
				if(NULL == new_vec)
				{
					free(mem);
					ERROR_LOG_GOTO(ERR, "Cannot insert new arg to the vector");
				}
				ret = new_vec;
			}
			begin = cmdline + 1;
		}
		switch(state)
		{
			case _NORMAL:
			    if(ch == '\\') state = _ESCAPE;
			    break;
			case _ESCAPE:
			    state = _NORMAL;
			    break;
		}
	} while(*(cmdline++) != 0);

	return ret;
ERR:
	_free_cmdline(ret);
	return NULL;
}

/**
 * @brief add a new servlet node in the service graph
 * @param vm the VM instance
 * @param service_reg the register that carries the referece to the target service
 * @param target_reg the register that we want to carry the result node object
 * @param name_reg the register carries the name
 * @param args_reg the register carries the param string
 * @param graphviz_reg the register carries the graphviz style info string
 * @return status code
 **/
static inline int _service_add_node(lang_vm_t* vm, uint32_t service_reg, uint32_t target_reg, uint32_t name_reg, uint32_t args_reg, uint32_t graphviz_reg)
{
	if(target_reg >= vm->num_reg)
	    ERROR_RETURN_LOG(int, "Invalid target register");

	_GET_PARAM(SERVICE, service);
	lang_vm_service_t* service = service_val->data.service;
	if(NULL == service) ERROR_RETURN_LOG(int, "Invalid service object");
	if(service->initialized)
	    ERROR_RETURN_LOG(int, "Cannot modify a service that has been already initialized");

	_GET_PARAM(STRID, name);
	const char* name = _get_string(vm, name_val);
	if(NULL == name) ERROR_RETURN_LOG(int, "Cannot get the servlet node name from the string table");

	_GET_PARAM(STRID, args);
	const char* args = _get_string(vm, args_val);
	if(NULL == args) ERROR_RETURN_LOG(int, "Cannot get the servlet arguments from the string table");

	const char* graphviz = NULL;
	if(graphviz_reg != ERROR_CODE(uint32_t))
	{
		const _value_t* val = _get_reg(vm, graphviz_reg);
		if(NULL == val) ERROR_RETURN_LOG(int, "Cannot read the graphviz register");
		if(LANG_VM_VALUE_TYPE_STRID != val->data.type)
		    ERROR_RETURN_LOG(int, "Invalid data type for grapviz prop");
		graphviz = _get_string(vm, val);
		if(NULL == graphviz) ERROR_RETURN_LOG(int, "Cannot get the node graphviz property from the string table");
	}

	vector_t* parsed_args = _parse_cmdline(args);
	if(NULL == parsed_args) ERROR_RETURN_LOG(int, "Cannot parse the argument list");
	runtime_stab_entry_t sid = runtime_stab_load((uint32_t)vector_length(parsed_args), VECTOR_GET_CONST(char const *, parsed_args, 0));
	_free_cmdline(parsed_args);
	if(ERROR_CODE(runtime_stab_entry_t) == sid)
	    ERROR_RETURN_LOG(int, "cannot load the servlet");

	sched_service_node_id_t nid = sched_service_buffer_add_node(service->buffer, sid);
	if(ERROR_CODE(sched_service_node_id_t) == nid)
	    ERROR_RETURN_LOG(int, "Cannot insert the servlet node to service buffer");

	_value_t* result = _value_new(vm);
	if(NULL == result)
	    ERROR_RETURN_LOG(int, "Cannot allocate new value for the result");

	result->data.type = LANG_VM_VALUE_TYPE_SERVLET;
	result->data.servlet.servlet = sid;
	result->data.servlet.node = nid;

	if(_decref(vm, vm->reg[target_reg]) == ERROR_CODE(int))
	    LOG_WARNING("Error when decref the register value");

	vm->reg[target_reg] = result;
	_incref(result);

	_servlet_t* servlet_node = (_servlet_t*)malloc(sizeof(_servlet_t));
	if(NULL == servlet_node)
	    ERROR_RETURN_LOG(int, "Cannot allocate memory for the servlet node");
	servlet_node->nid = nid;
	servlet_node->sid = sid;
	servlet_node->name = name;
	servlet_node->graphviz = graphviz != NULL ? graphviz : "";
	servlet_node->args = args;
	servlet_node->next = service->nodes;
	service->nodes = servlet_node;

	return 0;
}

/**
 * @brief start the service
 * @param vm the VM instance
 * @param serv_reg the register that carries the service register
 * @return status code
 **/
static inline int _service_start(lang_vm_t* vm, uint32_t serv_reg)
{
	_GET_PARAM(SERVICE, serv);
	lang_vm_service_t* service = serv_val->data.service;
	if(!service->initialized)
	{
		sched_service_t* ss = sched_service_from_buffer(service->buffer);
		if(NULL == ss) ERROR_RETURN_LOG(int, "Cannot build service");
		service->initialized = 1;
		if(sched_service_buffer_free(service->buffer) == ERROR_CODE(int))
		    LOG_WARNING("Cannot dispose the service buffer");
		service->buffer = NULL;

		service->service = ss;
	}

	if(sched_loop_start(service->service) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot start the service");

	return 0;
}

/**
 * @brief print a string to screen
 * @param vm the VM instance
 * @param reg the value register
 * @return status code
 **/
static inline int _echo(lang_vm_t* vm, uint32_t reg)
{
	_value_t* val = _get_reg(vm, reg);
	if(NULL == val) ERROR_RETURN_LOG(int, "Cannot get the value from register");

	size_t len = _value_string_len(vm, val);
	if(ERROR_CODE(size_t) == len)
	    ERROR_RETURN_LOG(int, "Cannot get the size of the value");

	char* _b = (char*)malloc(len + 1);
	if(NULL == _b) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate the buffer");

	string_buffer_t sbuf;
	string_buffer_open(_b, len + 1, &sbuf);

	if(_value_to_string(vm, val, &sbuf) == ERROR_CODE(int)) ERROR_LOG_GOTO(ERR, "Cannot convert the value to string");

	const char* result = string_buffer_close(&sbuf);

	if(NULL == result) ERROR_LOG_GOTO(ERR, "Cannot close the string buffer");

	puts(result);
	free(_b);
	return 0;
ERR:
	if(NULL != _b) free(_b);
	return ERROR_CODE(int);
}

/**
 * @brief insert a new module to the module addressing table
 * @param vm the VM instance
 * @param reg the module
 * @return status code
 **/
static inline int _insmod(lang_vm_t* vm, uint32_t reg)
{
	_value_t* val = _get_reg(vm, reg);
	if(NULL == val) ERROR_RETURN_LOG(int, "Cannot get the value from register");

	if((val->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) != LANG_VM_VALUE_TYPE_STRID)
	    ERROR_RETURN_LOG(int, "Invalid argument type, string expected");

	const char* mod_init_str = _get_string(vm, val);

	if(NULL == mod_init_str)
	    ERROR_RETURN_LOG(int, "Cannot read the register value");

	uint32_t arg_cap = 32;
	uint32_t argc = 0;
	char** argv = (char**)malloc(arg_cap * sizeof(char*));
	if(NULL == argv)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot create the argument buffer");

	const char* ptr, *begin;
	const itc_module_t* binary = NULL;
	int rc = 0;
	for(begin = ptr = mod_init_str;; ptr ++)
	{
		if(*ptr == ' ' || *ptr == 0)
		{
			if(ptr - begin > 0)
			{
				if(argc >= arg_cap)
				{
					char** new_argv = (char**)realloc(argv, sizeof(char*) * arg_cap * 2);
					if(new_argv == NULL)
					    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resize the argument buffer");
					argv = new_argv;
					arg_cap = arg_cap * 2;
				}

				argv[argc] = (char*)malloc((size_t)(ptr - begin + 1));
				if(NULL == argv[argc])
				    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allcoate memory for the argument string");

				memcpy(argv[argc], begin, (size_t)(ptr - begin));
				argv[argc][ptr-begin] = 0;
				argc ++;
			}
			begin = ptr + 1;
		}

		if(*ptr == 0) break;
	}


	binary = itc_binary_search_module(argv[0]);
	if(NULL == binary) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find the module binary named %s", argv[0]);

	LOG_DEBUG("Found module binary @%p", binary);

	if(ERROR_CODE(int) == itc_modtab_insmod(binary, argc - 1, (char const* const*) argv + 1))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot instantiate the mdoule binary using param %s", mod_init_str);

	goto CLEANUP;
ERR:
	rc = ERROR_CODE(int);
CLEANUP:

	if(NULL != argv)
	{
		uint32_t i;
		for(i = 0; i < argc; i ++)
		    free(argv[i]);

		free(argv);
	}

	return rc;
}

/**
 * @brief set the service input and output
 * @param vm the VM instance
 * @param out 0 if we are setting the input node, otherwise means we want to set the output node
 * @param serv_reg the register for the service
 * @param node_reg the register for the node register
 * @param pipe_reg the register for the pipe register
 * @return status code
 **/
static inline int _service_input_output(lang_vm_t* vm, int out, uint32_t serv_reg, uint32_t node_reg, uint32_t pipe_reg)
{
	_GET_PARAM(SERVICE, serv);
	lang_vm_service_t* service = serv_val->data.service;
	if(service->initialized)
	    ERROR_RETURN_LOG(int, "Cannot modify a initialized service");

	_GET_PARAM(SERVLET, node);
	_GET_PARAM(STRID, pipe);
	lang_vm_servlet_t servlet = node_val->data.servlet;

	const char* pipe = _get_string(vm, pipe_val);
	if(NULL == pipe) ERROR_RETURN_LOG(int, "Cannot get the pipe name from the string table");

	sched_service_node_id_t nid = servlet.node;
	runtime_api_pipe_id_t pid = runtime_stab_get_pipe(servlet.servlet, pipe);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pid)
	    ERROR_RETURN_LOG(int, "Cannot get the PD from the servlet table");
	if(out == 0)
	{
		if(ERROR_CODE(int) == sched_service_buffer_set_input(service->buffer, nid, pid))
		    ERROR_RETURN_LOG(int, "Cannot set the output servlet");
		service->input_node = nid;
		service->input_pipe = pid;
	}
	else
	{
		if(ERROR_CODE(int) == sched_service_buffer_set_output(service->buffer, nid, pid))
		    ERROR_RETURN_LOG(int, "Cannot set the output pipe");
		service->output_node = nid;
		service->output_pipe = pid;
	}
	return 0;
}

/**
 * @brief output the visualization of a service graph
 * @param vm the VM instance
 * @param serv_reg the register for the service object
 * @param file_reg the register for the file name
 * @return status cdoe
 **/
static inline int _service_visualize(lang_vm_t* vm, uint32_t serv_reg, uint32_t file_reg)
{
	const _servlet_t* node_ptr;
	const _pipe_t* pipe_ptr;
	int rc = ERROR_CODE(int);
	_GET_PARAM(SERVICE, serv);
	lang_vm_service_t* service = serv_val->data.service;
	_GET_PARAM(STRID, file);
	const char* file = _get_string(vm, file_val);
	if(NULL == file) ERROR_RETURN_LOG(int, "Cannot get the file string from the string table");
	FILE* fout = fopen(file, "w");
	if(NULL == fout) ERROR_RETURN_LOG_ERRNO(int, "Cannot open the output file");

	fprintf(fout, "digraph {\n");
	/* dump the nodes */
	for(node_ptr = service->nodes; NULL != node_ptr; node_ptr = node_ptr->next)
	{
		static char buffer[4096];
		string_buffer_t outbuf;
		string_buffer_open(buffer, sizeof(buffer), &outbuf);
		fprintf(fout, "\tnode_%u [shape = Mrecord, label = \"{{", node_ptr->nid);
		const runtime_pdt_t* pdt = runtime_stab_get_pdt(node_ptr->sid);
		if(NULL == pdt) ERROR_LOG_GOTO(CLEANUP, "Cannot get PDT for servlet instance SID = %u", node_ptr->sid);
		runtime_api_pipe_id_t i, limit = runtime_pdt_get_size(pdt);
		int first_input = 1, first_output = 1;
		if(ERROR_CODE(runtime_api_pipe_id_t) == limit) ERROR_LOG_GOTO(CLEANUP, "Cannot get the size of the PDT table");
		for(i = 0; i < limit; i ++)
		{
			runtime_api_pipe_flags_t pf = runtime_pdt_get_flags_by_pd(pdt, i);
			const char* name = runtime_pdt_get_name(pdt, i);
			if(NULL == name) ERROR_LOG_GOTO(CLEANUP, "Cannot get name for the pipe port");
			if(RUNTIME_API_PIPE_IS_INPUT(pf))
			{
				if(first_input) first_input = 0;
				else fprintf(fout, "|");
				fprintf(fout, "<P%u>%s", i, name);
			}
			else
			{
				if(strcmp("__null__", name) == 0 ||
				   strcmp("__error__", name) == 0)
					continue;

				if(first_output) first_output = 0;
				else string_buffer_append("|", &outbuf);
				string_buffer_appendf(&outbuf, "<P%u>%s", i, name);
			}
		}
		fprintf(fout, "}|%s|{%s}}\", %s];\n", node_ptr->name, string_buffer_close(&outbuf), node_ptr->graphviz);
	}

	/* dump the pipes */
	for(pipe_ptr = service->pipes; NULL != pipe_ptr; pipe_ptr = pipe_ptr->next)
	{
		for(node_ptr = service->nodes; NULL != node_ptr && pipe_ptr->pipe.source_node_id != node_ptr->nid; node_ptr = node_ptr->next);
		if(node_ptr == NULL) ERROR_LOG_GOTO(CLEANUP, "Cannot find the servlet node id");
		
		const runtime_pdt_t* pdt = runtime_stab_get_pdt(node_ptr->sid);
		if(NULL == pdt) ERROR_LOG_GOTO(CLEANUP, "Cannot get PDT for servlet instance SID = %u", node_ptr->sid);
		
		const char* name = runtime_pdt_get_name(pdt, RUNTIME_API_PIPE_TO_PID(pipe_ptr->pipe.source_pipe_desc));
		if(NULL == name) ERROR_LOG_GOTO(CLEANUP, "Cannot get name for the pipe port");
		
		if(strcmp(name, "__null__") == 0)
			fprintf(fout, "\tnode_%u->node_%u:P%u[style = dashed, color = blue];\n", 
				    pipe_ptr->pipe.source_node_id, 
		            pipe_ptr->pipe.destination_node_id, pipe_ptr->pipe.destination_pipe_desc);
		else if(strcmp(name, "__error__") == 0)
			fprintf(fout, "\tnode_%u->node_%u:P%u[style = dashed, color = red];\n", 
				    pipe_ptr->pipe.source_node_id, 
		            pipe_ptr->pipe.destination_node_id, pipe_ptr->pipe.destination_pipe_desc);
		else 
			fprintf(fout, "\tnode_%u:P%u->node_%u:P%u;\n", 
				    pipe_ptr->pipe.source_node_id, pipe_ptr->pipe.source_pipe_desc,
		            pipe_ptr->pipe.destination_node_id, pipe_ptr->pipe.destination_pipe_desc);
	}

	/* Input and output */
	if(service->input_node != ERROR_CODE(sched_service_node_id_t) &&
	   service->input_pipe != ERROR_CODE(runtime_api_pipe_id_t))
	{
		fprintf(fout, "\tinput [shape = ellipse];\n");
		fprintf(fout, "\tinput->node_%u:P%u;\n", service->input_node, service->input_pipe);
	}

	if(service->output_node != ERROR_CODE(sched_service_node_id_t) &&
	   service->output_pipe != ERROR_CODE(runtime_api_pipe_id_t))
	{
		fprintf(fout, "\toutput [shape = ellipse];\n");
		fprintf(fout, "\tnode_%u:P%u->output;\n", service->output_node, service->output_pipe);
	}

	fprintf(fout, "}\n");

	rc = 0;
CLEANUP:
	if(NULL != fout) fclose(fout);
	return rc;
}
#undef _GET_PARAM

/**
 * @brief dispose a used service
 * @param service the target service
 * @return status code
 **/
static inline int _service_free(lang_vm_service_t* service)
{
	if(NULL == service) return 0;
	int rc = 0;
	if(service->initialized == 0)
	    rc = sched_service_buffer_free(service->buffer);
	else
	    rc = sched_service_free(service->service);

	_servlet_t* serv;
	for(serv = service->nodes; NULL != serv;)
	{
		_servlet_t* tmp = serv;
		serv = serv->next;
		free(tmp);
	}

	_pipe_t* pipe;
	for(pipe = service->pipes; NULL != pipe;)
	{
		_pipe_t* tmp = pipe;
		pipe = pipe->next;
		free(tmp);
	}

	free(service);

	return rc;
}

/**
 * @brief dispose a used runtime value
 * @param value the value to dispose
 * @return status code
 **/
static inline int _value_free(_value_t* value)
{
	int rc = 0;
	switch(value->data.type)
	{
		case LANG_VM_VALUE_TYPE_RT_STR:
		    free(value->data.str);
		    goto FREE_VALUE;
		case LANG_VM_VALUE_TYPE_SERVICE:
		    _service_free(value->data.service);
FREE_VALUE:
		case LANG_VM_VALUE_TYPE_UNDEFINED:
		case LANG_VM_VALUE_TYPE_NUM:
		case LANG_VM_VALUE_TYPE_STRID:  /* because the string is actually allocate'd by the bytecode table */
		case LANG_VM_VALUE_TYPE_SERVLET:
		    free(value);
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Unknown value type %d", value->data.type);
	}

	return rc;
}

/**
 * @brief create a new runtime value
 * @param vm the target VM instance
 * @return the newly create value or NULL on error
 **/
static inline _value_t* _value_new(lang_vm_t* vm)
{
	_value_t* ret = (_value_t*)malloc(sizeof(_value_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new value");

	ret->data.type = LANG_VM_VALUE_TYPE_UNDEFINED;
	ret->refcnt = 0;
	ret->prev = NULL;
	ret->next = vm->values;
	if(vm->values != NULL) vm->values->prev = ret;
	vm->values = ret;

	return ret;
}

/**
 * @brief increase the reference counter of a value
 * @param value the value to incref
 * @return nothing
 **/
static inline void _incref(_value_t* value)
{
	value->refcnt ++;
}

/**
 * @brief decrease the reference counter for a value
 * @param vm the target VM instacen
 * @param value the value to decref
 * @return status code
 **/
static inline int _decref(lang_vm_t* vm, _value_t* value)
{
	if(NULL == value) return 0;
	if(value->refcnt == 0)
	    /* Keep going */;
	else
	    value->refcnt --;

	if(value->refcnt == 0)
	{
		LOG_DEBUG("Deallocating 0 reference'd value");
		if(value->prev == NULL)
		    vm->values = value->next;
		else
		    value->prev->next = value->next;
		if(value->next != NULL)
		    value->next->prev = value->prev;
		return _value_free(value);
	}
	return 0;
}

/**
 * @brief get a environment variable that has been registered in the property list
 * @param vm the VM instance
 * @param symid the symbol ID
 * @return the value of the environment variable or NULL
 **/
static inline _value_t* _get_env(lang_vm_t* vm, uint32_t symid)
{
	lang_prop_value_t value;
	lang_prop_type_t  type;
	int rc = lang_prop_get(vm->cb_vec, symid, &type, &value);
	if(ERROR_CODE(int) == rc)
	    ERROR_PTR_RETURN_LOG("Error when calling getter");
	if(rc == 1)
	{
		_value_t* ret = NULL;
		if(LANG_PROP_TYPE_INTEGER == type)
		{
			ret = _value_new(vm);
			if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create the new value");
			ret->data.type = LANG_VM_VALUE_TYPE_NUM;
			ret->data.num = value.integer;
		}
		else if(LANG_PROP_TYPE_STRING == type)
		{
			ret = _value_new(vm);
			if(NULL == ret)
			    ERROR_PTR_RETURN_LOG("Cannot create new value for the property");

			ret->data.type = LANG_VM_VALUE_TYPE_RT_STR;
			ret->data.str  = (char*)value.string;

		}
		return ret;
	}
	hashmap_find_res_t res;
	if(1 != hashmap_find(vm->env, &symid, sizeof(uint32_t), &res))
	    return _value_new(vm); /* The value should be undefined by default */

	return *(_value_t**)res.val_data;
}
/*
 * @brief set the variable in the environment
 * @param vm the VM object
 * @param symid the symbol id
 * @param incref indicates if we want the function to increase the refernce counter
 * @return status code */
static inline int _set_env(lang_vm_t* vm, uint32_t symid, _value_t* val, int incref)
{
	if((val->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_NUM)
	{
		int rc = lang_prop_set(vm->cb_vec, symid, LANG_PROP_TYPE_INTEGER, &val->data.num);
		if(ERROR_CODE(int) == rc) ERROR_RETURN_LOG(int, "Error when calling the setter");
		if(rc > 0) return 0;
	}
	else if((val->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_STRID)
	{
		const char* data = _get_string(vm, val);
		if(NULL == data)
		    ERROR_RETURN_LOG(int, "Cannot find string");
		int rc = lang_prop_set(vm->cb_vec, symid, LANG_PROP_TYPE_STRING, data);
		/* if we should incref here, the environment variable doesn't actuall hold the reference */
		if(rc > 0)
		{
			if(!incref && ERROR_CODE(int) == _decref(vm, val))
			    LOG_WARNING("Cannot decref the value");
			return 0;
		}
		else if(rc == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Error during invoking the setter function");
	}

	hashmap_find_res_t res;
	if(1 == hashmap_find(vm->env, &symid, sizeof(uint32_t), &res) && ERROR_CODE(int) == _decref(vm, *(_value_t**)res.val_data))
	    LOG_WARNING("Cannot decref the existing value");

	if(ERROR_CODE(int) == hashmap_insert(vm->env, &symid, sizeof(uint32_t), &val, sizeof(_value_t*), &res, 1))
	    ERROR_RETURN_LOG(int, "Cannot set the smybol to the given value");

	if(incref) _incref(val);

	return 0;
}

/**
 * @brief get the R-Value operand
 * @param vm the target VM instance
 * @param pc the program counter
 * @param n the n-th operand
 * @param incref if we need incref
 * @return the returned R-Value
 **/
static inline _value_t* _get_rvalue_operand(lang_vm_t*vm, uint32_t pc, uint32_t n, int incref)
{
	lang_bytecode_operand_id_t op;
	if(ERROR_CODE(int) == lang_bytecode_table_get_operand(vm->bc_table, pc, n, &op))
	    ERROR_PTR_RETURN_LOG("Cannot access the %u-th operand", n);

	_value_t* ret = NULL;
	switch(op.type)
	{
		case LANG_BYTECODE_OPERAND_GRAPHVIZ:
		case LANG_BYTECODE_OPERAND_STR:
		    if(NULL == (ret = _value_new(vm)))
		        ERROR_PTR_RETURN_LOG("Cannot create new value for the instant value");
		    ret->data.type = LANG_VM_VALUE_TYPE_STRID;
		    ret->data.strid = op.id;
		    break;
		case LANG_BYTECODE_OPERAND_SYM:
		    ret = _get_env(vm, op.id);
		    break;
		case LANG_BYTECODE_OPERAND_REG:
		    ret = _get_reg(vm, op.id);
		    break;
		case LANG_BYTECODE_OPERAND_INT:
		    if(NULL == (ret = _value_new(vm)))
		        ERROR_PTR_RETURN_LOG("Cannot create new value for the interger instance value");
		    ret->data.type = LANG_VM_VALUE_TYPE_NUM;
		    ret->data.num = op.num;
		    break;
		default:
		    ERROR_PTR_RETURN_LOG("Invalid operand type code");
	}

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot get the %d-th operand value", n);
	else if(incref) _incref(ret);

	return ret;
}

static inline int _get_bool_operand(lang_vm_t*vm, uint32_t pc, uint32_t n)
{
	int rc = ERROR_CODE(int);
	int ret;

	_value_t* cond = _get_rvalue_operand(vm, pc, n, 1);
	const char* str;
	switch(cond->data.type)
	{
		case LANG_VM_VALUE_TYPE_STRID:
		case LANG_VM_VALUE_TYPE_RT_STR:
		    if(NULL == (str = _get_string(vm, cond)))
		        ERROR_LOG_GOTO(RET, "Cannot get string");
		    if(strlen(str) != 0)
		        ret = 1;
		    else
		        ret = 0;
		    break;
		case LANG_VM_VALUE_TYPE_SERVICE:
		case LANG_VM_VALUE_TYPE_SERVLET:
		    ret = 1;
		    break;
		case LANG_VM_VALUE_TYPE_NUM:
		    if(cond->data.num)
		        ret = 1;
		    else
		        ret = 0;
		    break;
		case LANG_VM_VALUE_TYPE_UNDEFINED:
		    ret = 0;
		    break;
		default:
		    ERROR_LOG_GOTO(RET, "Invalid type code");
	}

	rc = ret;
RET:
	_decref(vm, cond);
	return rc;
}

/**
 * @brief set the L-Value operand
 * @param vm the target VM instance
 * @param pc the program counter
 * @param n the n-th operand
 * @param val the value to set
 * @param incref indicates if we need to run incref after the assignment
 * @return status code
 **/
static inline int _set_lvalue_operand(lang_vm_t* vm, uint32_t pc, uint32_t n, _value_t* val, int incref)
{
	lang_bytecode_operand_id_t op;
	if(ERROR_CODE(int) == lang_bytecode_table_get_operand(vm->bc_table, pc, n, &op))
	    ERROR_RETURN_LOG(int, "Cannot access the %u-th operand", n);

	if(op.type == LANG_BYTECODE_OPERAND_REG)  /* move to a register */
	{
		if(op.id >= vm->num_reg) ERROR_RETURN_LOG(int, "Invalid left register reference");
		if(_decref(vm, vm->reg[op.id]) == ERROR_CODE(int))
		    LOG_WARNING("Cannot decref the previous value");
		vm->reg[op.id] = val;
		if(incref) _incref(val);
	}
	else if(op.type == LANG_BYTECODE_OPERAND_SYM)
	{
		if(ERROR_CODE(int) == _set_env(vm, op.id, val, incref))
		    ERROR_RETURN_LOG(int, "Cannot finish the symbol assignment");
	}
	else ERROR_RETURN_LOG(int, "Invalid left operand type");

	return 0;
}

/**
 * @brief execute a move instruction
 * @param vm the VM instance
 * @param pc the program counter
 * @return status code
 **/
static inline int _exec_move(lang_vm_t* vm, uint32_t pc)
{
	if(2 != lang_bytecode_table_get_num_operand(vm->bc_table, pc)) ERROR_RETURN_LOG(int, "Invalid number of operands");
	/* We want to hold the refernce counter, otherwise, we decref first, it may dispose the object incorrectly*/
	_value_t* rvalue = _get_rvalue_operand(vm, pc, 1, 1);

	if(NULL == rvalue)
	    ERROR_LOG_GOTO(ERR, "Cannot get the R-Value");

	/* then we set the opernad */
	if(ERROR_CODE(int) == _set_lvalue_operand(vm, pc, 0, rvalue, 0))
	    ERROR_LOG_GOTO(ERR, "Cannot set the L-Value");

	return 0;
ERR:
	_decref(vm, rvalue);
	return ERROR_CODE(int);
}

static inline int _exec_boolean(lang_vm_t* vm, uint32_t pc)
{
	int opcode = lang_bytecode_table_get_opcode(vm->bc_table, pc);
	if(ERROR_CODE(int) == opcode) ERROR_RETURN_LOG(int, "Cannot get the opcode");

	if(3 != lang_bytecode_table_get_num_operand(vm->bc_table, pc))
	    ERROR_RETURN_LOG(int, "Invalid number of operands");

	int lv = _get_bool_operand(vm, pc, 1);
	if(ERROR_CODE(int) == lv) ERROR_RETURN_LOG(int, "Cannot get left value");
	int rv = _get_bool_operand(vm, pc, 2);
	if(ERROR_CODE(int) == rv) ERROR_RETURN_LOG(int, "Cannot get right value");

	int result = 0;

	switch(opcode)
	{
		case LANG_BYTECODE_OPCODE_AND:
		    result = lv && rv;
		    break;
		case LANG_BYTECODE_OPCODE_OR:
		    result = lv || rv;
		    break;
		case LANG_BYTECODE_OPCODE_XOR:
		    result = lv ^ rv;
		    break;
		default:
		    ERROR_RETURN_LOG(int, "Invalid opcode");
	}

	result = result ? 1 : 0;

	_value_t* res_val;
	if(NULL == (res_val = _value_new(vm)))
	    ERROR_RETURN_LOG(int, "Cannot create new value for the interger instance value");
	res_val->data.type = LANG_VM_VALUE_TYPE_NUM;
	res_val->data.num = result;

	if(_set_lvalue_operand(vm, pc, 0, res_val, 1) == ERROR_CODE(int))
	{
		_decref(vm, res_val);
		return ERROR_CODE(int);
	}

	return 0;
}

/**
 * @brief exeucte an arithmetic instruction
 * @param vm the VM instance
 * @param pc the program counter
 * @return status code
 **/
static inline int _exec_arithmetic(lang_vm_t* vm, uint32_t pc)
{
	if(3 != lang_bytecode_table_get_num_operand(vm->bc_table, pc)) ERROR_RETURN_LOG(int, "Invalid number of operands");

	int32_t lv;
	int32_t rv;
	int32_t res = 0;
	int op;

	_value_t* left = _get_rvalue_operand(vm, pc, 1, 1);
	_value_t* right = _get_rvalue_operand(vm, pc, 2, 1);
	if(NULL == left)
	    ERROR_LOG_GOTO(ERR, "Cannot get the left operand");
	if(NULL == right)
	    ERROR_LOG_GOTO(ERR, "Cannot get the right operand");

	if((left->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) != LANG_VM_VALUE_TYPE_NUM ||
	   (right->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) != LANG_VM_VALUE_TYPE_NUM)
	    ERROR_LOG_GOTO(ERR, "Type mismatch");

	lv = left->data.num;
	rv = right->data.num;

	_decref(vm, left);
	left = NULL;

	_decref(vm, right);
	right = NULL;

	switch((op = lang_bytecode_table_get_opcode(vm->bc_table, pc)))
	{
		case LANG_BYTECODE_OPCODE_ADD:
		    res = lv + rv;
		    break;
		case LANG_BYTECODE_OPCODE_SUB:
		    res = lv - rv;
		    break;
		case LANG_BYTECODE_OPCODE_MUL:
		    res = lv * rv;
		    break;
		case LANG_BYTECODE_OPCODE_DIV:
		case LANG_BYTECODE_OPCODE_MOD:
		    if(rv == 0) ERROR_LOG_GOTO(ERR, "Divide by zero");
		    if(op == LANG_BYTECODE_OPCODE_DIV) res = lv / rv;
		    else res = lv % rv;
		    break;
		case LANG_BYTECODE_OPCODE_NE:
		    res = (lv != rv);
		    break;
		case LANG_BYTECODE_OPCODE_EQ:
		    res = (lv == rv);
		    break;
		case LANG_BYTECODE_OPCODE_LT:
		    res = (lv < rv);
		    break;
		case LANG_BYTECODE_OPCODE_LE:
		    res = (lv <= rv);
		    break;
		case LANG_BYTECODE_OPCODE_GE:
		    res = (lv >= rv);
		    break;
		case LANG_BYTECODE_OPCODE_GT:
		    res = (lv > rv);
		    break;
		default:
		    ERROR_LOG_GOTO(ERR, "Invalid opcode");
	}

	_value_t* res_val;
	if(NULL == (res_val = _value_new(vm)))
	    ERROR_LOG_GOTO(ERR, "Cannot create new value for the interger instance value");
	res_val->data.type = LANG_VM_VALUE_TYPE_NUM;
	res_val->data.num = res;

	if(_set_lvalue_operand(vm, pc, 0, res_val, 1) == ERROR_CODE(int))
	{
		_decref(vm, res_val);
		return ERROR_CODE(int);
	}

	return 0;
ERR:
	if(left != NULL) _decref(vm, left);
	if(right != NULL) _decref(vm, right);
	return ERROR_CODE(int);
}

/**
 * @brief execute the add instruction
 * @note the add instruction is different than other arithmetic instructions, because it's the only instruction support
 *       string operations
 * @param vm the VM instance
 * @param pc the program counter
 * @return status code
 **/
static inline int _exec_arithmetic_implicit_convert(lang_vm_t* vm, uint32_t pc)
{
	if(3 != lang_bytecode_table_get_num_operand(vm->bc_table, pc)) ERROR_RETURN_LOG(int, "Invalid number of operands");

	char* ret = NULL;
	_value_t* res_val = NULL;
	_value_t* left = _get_rvalue_operand(vm, pc, 1, 1);
	_value_t* right = _get_rvalue_operand(vm, pc, 2, 1);
	string_buffer_t sbuf;

	if(NULL == left)  ERROR_LOG_GOTO(ERR, "Cannot get the left operand");
	if(NULL == right) ERROR_LOG_GOTO(ERR, "Cannot get the right operand");

	if((left->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_NUM &&
	   (right->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_NUM)
	{
		_decref(vm, left);
		_decref(vm, right);
		return _exec_arithmetic(vm, pc);
	}

	int opcode = lang_bytecode_table_get_opcode(vm->bc_table, pc);
	if(ERROR_CODE(int) == opcode) ERROR_LOG_GOTO(ERR, "Cannot get opcode");


	if((left->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_UNDEFINED ||
	   (right->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_UNDEFINED)
	{
		int res = 0;
		switch(opcode)
		{
			case LANG_BYTECODE_OPCODE_NE:
			    res = 1;
			case LANG_BYTECODE_OPCODE_EQ:
			    res ^= ((left->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_UNDEFINED &&
			            (right->data.type & LANG_VM_VALUE_TYPE_MASK_TYPE_CODE) == LANG_VM_VALUE_TYPE_UNDEFINED);
			    break;
			default:
			    ERROR_LOG_GOTO(ERR, "Unsupported operation on undefined data type");
		}
		if(NULL == (res_val = _value_new(vm)))
		    ERROR_LOG_GOTO(ERR, "Cannot create new value for the interger instance value");
		res_val->data.type = LANG_VM_VALUE_TYPE_NUM;
		res_val->data.num = res;
	}
	else if(opcode == LANG_BYTECODE_OPCODE_ADD)
	{
		size_t left_len, right_len;
		if(ERROR_CODE(size_t) == (left_len = _value_string_len(vm, left)))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the length of the left operand");

		if(ERROR_CODE(size_t) == (right_len = _value_string_len(vm, right)))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the length of the left operand");

		ret = (char*)malloc(left_len + right_len + 1);
		if(NULL == ret)  ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the new string");

		string_buffer_open(ret, left_len + right_len + 1, &sbuf);
		if(ERROR_CODE(int) == _value_to_string(vm, left, &sbuf))
		    ERROR_LOG_GOTO(ERR, "Cannot convert left operand to string");
		if(ERROR_CODE(int) == _value_to_string(vm, right, &sbuf))
		    ERROR_LOG_GOTO(ERR, "Cannot convert right operand to string");

		string_buffer_close(&sbuf);

		if(NULL == (res_val = _value_new(vm)))
		    ERROR_LOG_GOTO(ERR, "Cannot create new value for the interger instance value");
		res_val->data.type = LANG_VM_VALUE_TYPE_RT_STR;
		res_val->data.str = ret;
	}
	else
	{
		char* lv = NULL;
		char* rv = NULL;
		int res, cmp;
		size_t left_len, right_len;

		if(ERROR_CODE(size_t) == (left_len = _value_string_len(vm, left)))
		    ERROR_LOG_ERRNO_GOTO(LERR, "Cannot get the length of the left operand");

		if(ERROR_CODE(size_t) == (right_len = _value_string_len(vm, right)))
		    ERROR_LOG_ERRNO_GOTO(LERR, "Cannot get the length of the left operand");

		if(NULL == (lv = (char*)malloc(left_len + 1)))
		    ERROR_LOG_ERRNO_GOTO(LERR, "Cannot allocate memory for the left value");
		if(NULL == (rv = (char*)malloc(right_len + 1)))
		    ERROR_LOG_ERRNO_GOTO(LERR, "Cannot allocate memory for the right value");

		string_buffer_t lb, rb;
		string_buffer_open(lv, left_len + 1, &lb);
		string_buffer_open(rv, right_len + 1, &rb);

		if(ERROR_CODE(int) == _value_to_string(vm, left, &lb))
		    ERROR_LOG_GOTO(LERR, "Cannot convert left operand to string");
		if(ERROR_CODE(int) == _value_to_string(vm, right, &rb))
		    ERROR_LOG_GOTO(LERR, "Cannot convert right operad to string");

		string_buffer_close(&lb);
		string_buffer_close(&rb);

		cmp = strcmp(lv, rv);

		switch(opcode)
		{
			case LANG_BYTECODE_OPCODE_NE:
			    res = (cmp != 0);
			    break;
			case LANG_BYTECODE_OPCODE_EQ:
			    res = (cmp == 0);
			    break;
			case LANG_BYTECODE_OPCODE_LT:
			    res = (cmp < 0);
			    break;
			case LANG_BYTECODE_OPCODE_LE:
			    res = (cmp <= 0);
			    break;
			case LANG_BYTECODE_OPCODE_GT:
			    res = (cmp > 0);
			    break;
			case LANG_BYTECODE_OPCODE_GE:
			    res = (cmp >= 0);
			    break;
			default:
			    ERROR_LOG_GOTO(LERR, "Invalid opcode");
		}

		if(NULL == (res_val = _value_new(vm)))
		    ERROR_LOG_GOTO(LERR, "Cannot create new value for the interger instance value");

		res_val->data.type = LANG_VM_VALUE_TYPE_NUM;
		res_val->data.num = res;

		free(lv);
		free(rv);
		goto RET;
LERR:
		if(NULL == lv) free(lv);
		if(NULL == rv) free(rv);
	}

RET:

	if(ERROR_CODE(int) == _set_lvalue_operand(vm, pc, 0, res_val, 1)) goto ERR;

	_decref(vm, left);
	_decref(vm, right);

	return 0;
ERR:
	if(ret != NULL) free(ret);
	if(left != NULL) _decref(vm, left);
	if(right != NULL) _decref(vm, right);
	if(res_val != NULL)_decref(vm, res_val);
	return ERROR_CODE(int);
}


/**
 * @brief execute pusharg instruction, append a new param in the param cache of the VM
 * @param vm the VM instance
 * @param pc the program pointer
 * @return status code
 **/
static inline int _exec_pusharg(lang_vm_t* vm, uint32_t pc)
{
	if(1 != lang_bytecode_table_get_num_operand(vm->bc_table, pc)) ERROR_RETURN_LOG(int, "Invalid number of operands");
	lang_bytecode_operand_id_t left;
	if(ERROR_CODE(int) == lang_bytecode_table_get_operand(vm->bc_table, pc, 0, &left))
	    ERROR_RETURN_LOG(int, "Cannot acquire the first operand");
	if(left.type != LANG_BYTECODE_OPERAND_REG)
	    ERROR_RETURN_LOG(int, "Wrong operand type");

	vector_t* new = vector_append(vm->params, &left.id);
	if(NULL == new)
	    ERROR_RETURN_LOG(int, "Cannot push the argument to the argument buffer");

	vm->params = new;

	return 0;
}

/**
 * @brief execute jump instruction
 * @param vm the VM context
 * @param pc the program pointer
 * @return status code
 **/
static inline int _exec_jump(lang_vm_t* vm, uint32_t* pc)
{
	int op;
	if(ERROR_CODE(int) == (op = lang_bytecode_table_get_opcode(vm->bc_table, *pc)))
	    ERROR_RETURN_LOG(int, "Cannot read the opcode");

	_value_t* target = NULL;
	int jump = 1;
	if(op == LANG_BYTECODE_OPCODE_JUMP)
	{
		if(NULL == (target = _get_rvalue_operand(vm, *pc, 0, 1)))
		    ERROR_RETURN_LOG(int, "Cannot read targetoperand");
	}
	else if(op == LANG_BYTECODE_OPCODE_JZ)
	{
		if(NULL == (target = _get_rvalue_operand(vm, *pc, 1, 1)))
		    ERROR_RETURN_LOG(int, "Cannot read target operand");
		int rc = _get_bool_operand(vm, *pc, 0);
		if(ERROR_CODE(int) == rc) ERROR_LOG_GOTO(ERR, "Cannot get the cond operand");
		if(rc) jump = 0;
	}

	if(target->data.type != LANG_VM_VALUE_TYPE_NUM)
	    ERROR_LOG_GOTO(ERR, "Invalid operand type number expected");

	uint32_t address = (uint32_t)target->data.num;

	if(jump) *pc = address - 1;

	_decref(vm, target);
	return 0;
ERR:
	if(NULL != target) _decref(vm, target);
	return ERROR_CODE(int);
}

/**
 * @brief execute moveundef instruction
 * @param vm the VM context
 * @param pc the program counter
 * return status code
 **/
static inline int _exec_undefined(lang_vm_t* vm, uint32_t pc)
{
	_value_t* val = _value_new(vm);

	if(NULL == val) ERROR_RETURN_LOG(int, "Cannot create new undefined value");

	_incref(val);

	if(_set_lvalue_operand(vm, pc, 0, val, 0) == ERROR_CODE(int))
	{
		_decref(vm, val);
		ERROR_RETURN_LOG(int, "Cannot set the target register");
	}

	return 0;
}

/**
 * @brief read the N-th param in the param cache of the VM
 * @param vm the VM instance
 * @param n the N-th param
 * @return the register reference that carries the param
 **/
static inline uint32_t _get_param(lang_vm_t* vm, uint32_t n)
{
	if(n >= vector_length(vm->params)) return ERROR_CODE(uint32_t);
	return *(uint32_t*)VECTOR_GET_CONST(uint32_t, vm->params, n);
}

/**
 * @brief execute the function invocation
 * @param vm the VM instance
 * @param pc the program counter
 * @return status code
 **/
static inline int _exec_invoke(lang_vm_t* vm, uint32_t pc)
{
	if(2 != lang_bytecode_table_get_num_operand(vm->bc_table, pc)) ERROR_RETURN_LOG(int, "Invalid number of operands");
	lang_bytecode_operand_id_t left, right;
	if(ERROR_CODE(int) == lang_bytecode_table_get_operand(vm->bc_table, pc, 0, &left))
	    ERROR_RETURN_LOG(int, "Cannot acquire the first operand");
	if(ERROR_CODE(int) == lang_bytecode_table_get_operand(vm->bc_table, pc, 1, &right))
	    ERROR_RETURN_LOG(int, "Cannot acquire the second operand");

	if(left.type != LANG_BYTECODE_OPERAND_REG)
	    ERROR_RETURN_LOG(int, "Wrong operand type");
	if(right.type != LANG_BYTECODE_OPERAND_BUILTIN)
	    ERROR_RETURN_LOG(int, "Wrong operand type");

	if(left.id >= vm->num_reg)
	    ERROR_RETURN_LOG(int, "Invalid register reference");

	_value_t* result = NULL;
	switch(right.id)
	{
		case LANG_BYTECODE_BUILTIN_NEW_GRAPH:
		    if(NULL == (result = _value_new(vm)))
		        ERROR_RETURN_LOG(int, "Cannot create value for the new service");
		    if(NULL == (result->data.service = _service_new()))
		    {
			    _value_free(result);
			    ERROR_RETURN_LOG(int, "Cannot create new service object");
		    }
		    result->data.type = LANG_VM_VALUE_TYPE_SERVICE;
		    break;
		case LANG_BYTECODE_BUILTIN_ADD_EDGE:
		    if(ERROR_CODE(int) == _service_add_pipe(vm, _get_param(vm, 0), _get_param(vm, 1), _get_param(vm, 2), _get_param(vm, 3), _get_param(vm, 4)))
		        ERROR_RETURN_LOG(int, "Cannot add a pipe");
		    break;
		case LANG_BYTECODE_BUILTIN_ADD_NODE:
		    if(ERROR_CODE(int) == _service_add_node(vm, _get_param(vm, 0), left.id, _get_param(vm, 1), _get_param(vm, 2), _get_param(vm, 3)))
		        ERROR_RETURN_LOG(int, "Cannot add a node");
		    break;
		case LANG_BYTECODE_BUILTIN_INPUT:
		    if(ERROR_CODE(int) == _service_input_output(vm, 0, _get_param(vm, 0), _get_param(vm, 1), _get_param(vm, 2)))
		        ERROR_RETURN_LOG(int, "Cannot set input");
		    break;
		case LANG_BYTECODE_BUILTIN_OUTPUT:
		    if(ERROR_CODE(int) == _service_input_output(vm, 1, _get_param(vm, 0), _get_param(vm, 1), _get_param(vm, 2)))
		        ERROR_RETURN_LOG(int, "Cannot set output");
		    break;
		case LANG_BYTECODE_BUILTIN_GRAPHVIZ:
		    if(ERROR_CODE(int) == _service_visualize(vm, _get_param(vm, 0), _get_param(vm, 1)))
		        ERROR_RETURN_LOG(int, "Cannot visualize the service");
		    break;
		case LANG_BYTECODE_BUILTIN_START:
		    if(ERROR_CODE(int) == _service_start(vm, _get_param(vm, 0)))
		        ERROR_RETURN_LOG(int, "Cannot start the service");
		    break;
		case LANG_BYTECODE_BUILTIN_ECHO:
		    if(ERROR_CODE(int) == _echo(vm, _get_param(vm, 0)))
		        ERROR_RETURN_LOG(int, "Cannot call echo built-in funciton");
		    break;
		case LANG_BYTECODE_BUILTIN_INSMOD:
		    if(ERROR_CODE(int) == _insmod(vm, _get_param(vm, 0)))
		        ERROR_RETURN_LOG(int, "Cannot call insmod built-in function");
		    break;
		default:
		    LOG_WARNING("Fixme: unsupported builtin function type");
	}

	if(NULL != result)
	{
		if(ERROR_CODE(int) == _decref(vm, vm->reg[left.id]))
		    LOG_WARNING("Failed to decref the target register");
		vm->reg[left.id] = result;
		_incref(result);
	}

	return 0;
}

int lang_vm_free(lang_vm_t* vm)
{
	int rc = 0;

	if(NULL == vm) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != vm->env && ERROR_CODE(int) == hashmap_free(vm->env))
	    rc = ERROR_CODE(int);

	if(NULL != vm->params && ERROR_CODE(int) == vector_free(vm->params))
	    rc = ERROR_CODE(int);

	if(NULL != vm->cb_vec && ERROR_CODE(int) == lang_prop_callback_vector_free(vm->cb_vec))
	    rc = ERROR_CODE(int);

	_value_t* ptr = vm->values;
	for(;NULL != ptr;)
	{
		_value_t* tmp = ptr;
		ptr = ptr->next;
		_value_free(tmp);
	}


	free(vm);
	return rc;
}

int lang_vm_exec(lang_vm_t* vm)
{
	if(NULL == vm) ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t pc = 0; /* the program counter */
	uint32_t pc_limit = lang_bytecode_table_get_num_bytecode(vm->bc_table);
	if(ERROR_CODE(uint32_t) == pc_limit) ERROR_RETURN_LOG(int, "Cannot get the length of bytecode table");
	for(;pc < pc_limit; pc ++)
	{
		int opcode = lang_bytecode_table_get_opcode(vm->bc_table, pc);
		if(ERROR_CODE(int) == opcode)
		    ERROR_RETURN_LOG(int, "Cannot read the bytecode opcode from the bytecode table");

#ifdef LOG_DEBUG_ENABLED
		static char instruction_buffer[1024];
		string_buffer_t sbuf;
		string_buffer_open(instruction_buffer, sizeof(instruction_buffer), &sbuf);
		if(ERROR_CODE(int) != lang_bytecode_table_append_to_string_buffer(vm->bc_table, pc, &sbuf))
		    LOG_DEBUG("Executing instruction: %.8x\t%s", pc, string_buffer_close(&sbuf));
		else
		    LOG_WARNING("Cannot print current instruction");
#endif
		int rc;
		switch(opcode)
		{
			case LANG_BYTECODE_OPCODE_MOVE:
			    rc = _exec_move(vm, pc);
			    break;
			case LANG_BYTECODE_OPCODE_PUSHARG:
			    rc = _exec_pusharg(vm, pc);
			    break;
			case LANG_BYTECODE_OPCODE_INVOKE:
			    rc = _exec_invoke(vm, pc);
			    vector_clear(vm->params);
			    break;
			case LANG_BYTECODE_OPCODE_SUB:
			case LANG_BYTECODE_OPCODE_MUL:
			case LANG_BYTECODE_OPCODE_DIV:
			case LANG_BYTECODE_OPCODE_MOD:
			    rc = _exec_arithmetic(vm, pc);
			    break;
			case LANG_BYTECODE_OPCODE_ADD:
			case LANG_BYTECODE_OPCODE_NE:
			case LANG_BYTECODE_OPCODE_EQ:
			case LANG_BYTECODE_OPCODE_LT:
			case LANG_BYTECODE_OPCODE_GT:
			case LANG_BYTECODE_OPCODE_LE:
			case LANG_BYTECODE_OPCODE_GE:
			    rc = _exec_arithmetic_implicit_convert(vm, pc);
			    break;
			case LANG_BYTECODE_OPCODE_JUMP:
			case LANG_BYTECODE_OPCODE_JZ:
			    rc = _exec_jump(vm, &pc);
			    break;
			case LANG_BYTECODE_OPCODE_AND:
			case LANG_BYTECODE_OPCODE_OR:
			case LANG_BYTECODE_OPCODE_XOR:
			    rc = _exec_boolean(vm, pc);
			    break;
			case LANG_BYTECODE_OPCODE_UNDEFINED:
			    rc = _exec_undefined(vm, pc);
			    break;
			default:
			    LOG_ERROR("Invalid opcode %d", opcode);
			    rc = ERROR_CODE(int);
		}

		if(ERROR_CODE(int) == rc)
		    ERROR_RETURN_LOG(int, "Error during executing the instruction");
	}

	return 0;
}
