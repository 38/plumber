/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

#include <error.h>

#include <package_config.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/comp/lex.h>
#include <pss/comp/env.h>
#include <pss/comp/comp.h>

#include <pss/comp/block.h>

#define _LOOKAHEAD 3

/**
 * @brief Represent a control block info
 **/
typedef struct {
	pss_bytecode_segment_t*  seg;      /*!< The code segment which contains this control block */
	pss_bytecode_addr_t      begin;    /*!< The begin address inside the code block */
	pss_bytecode_label_t     end;      /*!< The end label of the code block */
	uint32_t                 loop:1;   /*!< If this block is a loop */
} _control_block_t;

/**
 * @brief The actual data structure for a compiler instance
 **/
struct _pss_comp_t {
	pss_comp_lex_t*          lexer;            /*!< The lexer we are using */
	pss_bytecode_module_t*   module;           /*!< The output bytecode module */
	pss_comp_env_t*          env;              /*!< The compile time environment abstraction */
	pss_comp_error_t**       error_buf;        /*!< The last compiler error */
	pss_comp_lex_token_t     ahead[_LOOKAHEAD];/*!< The lookahead ring buffer */
	uint32_t                 ahead_begin;      /*!< The ring buffer begin */
	pss_bytecode_segment_t*  seg_stack[PSS_COMP_ENV_SCOPE_MAX];  /*!< The current code segment the compiler is writting */
	uint32_t                 seg_stack_top;    /*!< The stack top for the current segment ID */
	_control_block_t         ctl_stack[PSS_COMP_ENV_SCOPE_MAX];    /*!< The control block stack */
	uint32_t                 ctl_stack_top;/*!< The top pointer of the control block stack */
};

int pss_comp_compile(pss_comp_option_t* option, pss_comp_error_t** error)
{
	int rc = ERROR_CODE(int);
	if(NULL == option || NULL == option->lexer || NULL == option->module)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_comp_t compiler = {
		.lexer = option->lexer,
		.module = option->module,
		.error_buf = error,
		.ahead_begin = 0
	};

	pss_bytecode_segid_t entry_point;

	uint32_t i;
	for(i = 0; i < _LOOKAHEAD; i ++)
	compiler.ahead[i].type = PSS_COMP_LEX_TOKEN_NAT;

	if(NULL == (compiler.env = pss_comp_env_new()))
		PSS_COMP_RAISE_GOTO(ERR, &compiler, "Internal Error: Cannot creat environment");

	if(ERROR_CODE(int) == pss_comp_open_closure(&compiler, 0, NULL))
		PSS_COMP_RAISE_GOTO(ERR, &compiler, "Internal Error: Cannot open closure for the module init");

	if(ERROR_CODE(int) == pss_comp_block_parse(&compiler, PSS_COMP_LEX_TOKEN_NAT, PSS_COMP_LEX_TOKEN_EOF, NULL))
		ERROR_LOG_GOTO(ERR, "Cannot parse the top-level code block");


	if(ERROR_CODE(pss_bytecode_regid_t) == (entry_point = pss_comp_close_closure(&compiler)))
		PSS_COMP_RAISE_GOTO(ERR, &compiler, "Internal Error: Cannot close closure for the module init");

	if(ERROR_CODE(int) == pss_bytecode_module_set_entry_point(compiler.module, entry_point))
		PSS_COMP_RAISE_GOTO(ERR, &compiler, "Internal Error: Cannot set up the module entry point");

	rc = 0;
ERR:
	if(NULL != compiler.env) pss_comp_env_free(compiler.env);
	return rc;
}

int pss_comp_raise(pss_comp_t* comp, const char* msg, ...)
{
	if(NULL == comp || NULL == msg) ERROR_RETURN_LOG(int, "Invalid arguments");
	va_list ap;
	va_start(ap, msg);
	int bytes_required = vsnprintf(NULL, 0, msg, ap);
	pss_comp_error_t* err = (pss_comp_error_t*)malloc(sizeof(pss_comp_error_t) + (unsigned)bytes_required + 1);
	if(NULL == err)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the error message");
	vsnprintf(err->message, (unsigned)bytes_required + 1, msg, ap);
	err->next = *comp->error_buf;
	*comp->error_buf = err;
	err->filename = pss_comp_peek(comp,0)->file;
	err->line = pss_comp_peek(comp,0)->line;
	va_end(ap);
	return 0;
ERR:
	return ERROR_CODE(int);
}

int pss_comp_free_error(pss_comp_error_t* error)
{
	if(NULL == error) ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_comp_error_t* this;
	for(;NULL != error->next;)
	{
		this = error;
		error = error->next;
		free(this);
	}

	return 0;
}

const pss_comp_lex_token_t* pss_comp_peek(pss_comp_t* comp, uint32_t n)
{
	if(NULL == comp || n >= _LOOKAHEAD)
		PSS_COMP_RAISE_RETURN_PTR(comp, "Internal Error: Invalid arguments");

	uint32_t i;
	for(i = 0; i <= n; i ++)
	{
		uint32_t off = (i + comp->ahead_begin) % _LOOKAHEAD;
		if(comp->ahead[off].type == PSS_COMP_LEX_TOKEN_NAT)
		{
			if(pss_comp_lex_next_token(comp->lexer, comp->ahead + off) == ERROR_CODE(int))
				PSS_COMP_RAISE_RETURN_PTR(comp, "Internal Error: Cannot peek lexer token");
		}
	}
	return comp->ahead + (n + comp->ahead_begin) % _LOOKAHEAD;
}

int pss_comp_comsume(pss_comp_t* comp, uint32_t n)
{
	if(n > _LOOKAHEAD)
	{
		LOG_WARNING("Consume %u tokens is not valid because is larger than the LookAhead buffer", n);
		n = _LOOKAHEAD;
	}
	uint32_t i;
	for(i = 0; i < n; i ++)
		comp->ahead[(i + comp->ahead_begin) % _LOOKAHEAD].type = PSS_COMP_LEX_TOKEN_NAT;
	comp->ahead_begin = (comp->ahead_begin + n) % _LOOKAHEAD;
	return 0;
}

pss_bytecode_segment_t* pss_comp_get_code_segment(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN_PTR(comp, "Internal Error: Invalid arguments");

	if(comp->seg_stack_top == 0) 
		PSS_COMP_RAISE_RETURN_PTR(comp, "Internal Error: Compiler is curarently out of closure");

	return comp->seg_stack[comp->seg_stack_top - 1];
}

int pss_comp_open_closure(pss_comp_t* comp, uint32_t nargs, char const** argnames)
{
	if(NULL == comp || ERROR_CODE(uint32_t) == nargs || (NULL == argnames && nargs > 0))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	if(ERROR_CODE(int) == pss_comp_env_open_scope(comp->env))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot open new scope for closure");

	pss_bytecode_regid_t argid[nargs];
	pss_bytecode_segment_t* segment = NULL;
	uint32_t i;
	for(i = 0; i < nargs; i ++)
		if(1 != pss_comp_env_get_var(comp->env, argnames[i], 1, argid + i))
			PSS_COMP_RAISE_GOTO(ERR, comp, "Internal Error: Cannot allocate register for the argument");
			
	if(NULL == (segment = pss_bytecode_segment_new((pss_bytecode_regid_t)nargs, argid)))
		PSS_COMP_RAISE_GOTO(ERR, comp, "Internal Error: Cannot create new code segment for the segment");

	comp->seg_stack[comp->seg_stack_top ++] = segment;

	return 0;
ERR:
	pss_comp_env_close_scope(comp->env);
	if(segment != NULL)
		pss_bytecode_segment_free(segment);
	return ERROR_CODE(int);
}

pss_bytecode_segid_t pss_comp_close_closure(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal Error: Invalid arguments");
	if(comp->seg_stack_top == 0)
		PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal Error: Compiler is current out of closure");

	if(ERROR_CODE(int) == pss_comp_env_close_scope(comp->env))
		PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal Error: Cannot close current scope");

	pss_bytecode_segment_t* seg = comp->seg_stack[comp->seg_stack_top - 1];

	/* Finally, we need to append the return sentinel to the segment, so that the VM won't get to somewhere undefined */
	pss_bytecode_addr_t guard_addr;
	guard_addr = pss_bytecode_segment_length(seg);
	if(ERROR_CODE(pss_bytecode_addr_t) == guard_addr) 
		PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal error: Cannot intert guard instruction");

	do{
		pss_bytecode_instruction_t inst;
		if(guard_addr > 0 && ERROR_CODE(int) != pss_bytecode_segment_get_inst(seg, guard_addr - 1, &inst) && 
		   inst.info->operation == PSS_BYTECODE_OP_RETURN) break;

		if(ERROR_CODE(pss_bytecode_addr_t) == (guard_addr = pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_UNDEF_LOAD, PSS_BYTECODE_ARG_REGISTER(0), PSS_BYTECODE_ARG_END)))
			PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal Error: Cannot append the sentinel instruction to the code segment");
		if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_RETURN, PSS_BYTECODE_ARG_REGISTER(0), PSS_BYTECODE_ARG_END))
			PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal Error: Cannot append the sentinel instruction to the code segment");
	} while(0);

	for(;comp->ctl_stack_top > 0 && 
		 comp->ctl_stack[comp->ctl_stack_top - 1].seg == seg;
		 comp->ctl_stack_top --)
		if(ERROR_CODE(int) == pss_bytecode_segment_patch_label(seg, comp->ctl_stack[comp->ctl_stack_top - 1].end, guard_addr))
			PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal Error: Cannot patch the unclosed control block");

	pss_bytecode_segid_t ret = pss_bytecode_module_append(comp->module, comp->seg_stack[--comp->seg_stack_top]);
	if(ERROR_CODE(pss_bytecode_segid_t) == ret)
		PSS_COMP_RAISE_RETURN(pss_bytecode_segid_t, comp, "Internal Error: Cannot put the segment to the bytecode module");

	return ret;
}

int pss_comp_open_control_block(pss_comp_t* comp, int loop)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");
	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);

	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get the code segment for current compiler context");

	pss_bytecode_addr_t begin = pss_bytecode_segment_length(seg);

	if(ERROR_CODE(pss_bytecode_addr_t) == begin)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot get the length of the code segment");

	pss_bytecode_label_t end = pss_bytecode_segment_label_alloc(seg);
	if(ERROR_CODE(pss_bytecode_label_t) == end)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot allocate the end label for the control block");

	comp->ctl_stack[comp->ctl_stack_top].begin = begin;
	comp->ctl_stack[comp->ctl_stack_top].end   = end;
	comp->ctl_stack[comp->ctl_stack_top].seg   = seg;
	comp->ctl_stack[comp->ctl_stack_top].loop  = loop ? 1u : 0u;  
	comp->ctl_stack_top ++;

	return 0;
}

int pss_comp_close_control_block(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");
	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);

	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get the code segment for current compiler context");
	if(0 == comp->ctl_stack_top || comp->ctl_stack[comp->ctl_stack_top - 1].seg != seg)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Compiler is currently outof control block");

	pss_bytecode_addr_t current = pss_bytecode_segment_length(seg);
	if(ERROR_CODE(pss_bytecode_addr_t) == current)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot get current instruction address");

	if(ERROR_CODE(int) == pss_bytecode_segment_patch_label(seg, comp->ctl_stack[comp->ctl_stack_top - 1].end, current))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot patch the labeled instructions");


	comp->ctl_stack_top --;

	return 0;
}

pss_bytecode_addr_t pss_comp_last_loop_begin(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(pss_bytecode_addr_t, comp, "Internal Error: Invalid arguments");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Cannot get the code segment for current compiler context");

	uint32_t level = comp->ctl_stack_top;
	for(;level > 0 && seg == comp->ctl_stack[level - 1].seg; level --)
		if(comp->ctl_stack[level - 1].loop) 
			return comp->ctl_stack[level - 1].begin;

	PSS_COMP_RAISE_RETURN(pss_bytecode_addr_t, comp, "Internal error: Getting a loop address from the outside of the loop");
}

pss_bytecode_label_t pss_comp_last_loop_end(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(pss_bytecode_label_t, comp, "Internal Error: Invalid arguments");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Cannot get the code segment for current compiler context");

	uint32_t level = comp->ctl_stack_top;
	for(;level > 0 && seg == comp->ctl_stack[level - 1].seg; level --)
		if(comp->ctl_stack[level - 1].loop) 
			return comp->ctl_stack[level - 1].end;

	PSS_COMP_RAISE_RETURN(pss_bytecode_label_t, comp, "Internal error: Getting a loop address from the outside of the loop");
}

pss_bytecode_addr_t pss_comp_last_control_block_begin(pss_comp_t* comp, uint32_t n)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(pss_bytecode_addr_t, comp, "Internal Error: Invalid arguments");

	if(comp->ctl_stack_top <= n) PSS_COMP_RAISE_RETURN(pss_bytecode_addr_t, comp, "Internal error: Invalid control block");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Cannot get the code segment for current compiler context");

	if(0 == comp->ctl_stack_top || seg != comp->ctl_stack[comp->ctl_stack_top - 1].seg)
		PSS_COMP_RAISE_RETURN(pss_bytecode_addr_t, comp, "Internal Error: The compiler is currently out of control block");

	return comp->ctl_stack[comp->ctl_stack_top - 1 - n].begin;
}

pss_bytecode_label_t pss_comp_last_control_block_end(pss_comp_t* comp, uint32_t n)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(pss_bytecode_label_t, comp, "Internal Error: Invalid arguments");
	
	if(comp->ctl_stack_top <= n) PSS_COMP_RAISE_RETURN(pss_bytecode_label_t, comp, "Internal error: Invalid control block");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);

	if(NULL == seg) ERROR_RETURN_LOG(pss_bytecode_label_t, "Cannot get the code segment for current compiler context");
	if(0 == comp->ctl_stack_top || seg != comp->ctl_stack[comp->ctl_stack_top - 1].seg)
		PSS_COMP_RAISE_RETURN(pss_bytecode_label_t, comp, "Internal Error: The compiler is currently out of control block");

	return comp->ctl_stack[comp->ctl_stack_top - 1 - n].end;
}

pss_comp_env_t* pss_comp_get_env(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN_PTR(comp, "Internal Error: Invalid arguments");

	if(NULL == comp->env) PSS_COMP_RAISE_RETURN_PTR(comp, "Internal Error: Invalid enviroment");

	return comp->env;
}

int pss_comp_open_scope(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	if(ERROR_CODE(int) == pss_comp_env_open_scope(comp->env))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot open a new scope");

	return 0;
}

int pss_comp_close_scope(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	if(ERROR_CODE(int) == pss_comp_env_close_scope(comp->env))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Canno close current scope");

	return 0;
}

int pss_comp_get_local_var(pss_comp_t* comp, const char* var, pss_bytecode_regid_t* resbuf)
{
	if(NULL == comp || NULL == var)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	int rc = pss_comp_env_get_var(comp->env, var, 0, resbuf);
	if(ERROR_CODE(int) == rc)
		PSS_COMP_RAISE_RETURN(int, comp, "Cannot get the local variable from the environment");

	return rc;
}

pss_bytecode_regid_t pss_comp_decl_local_var(pss_comp_t* comp, const char* var)
{
	if(NULL == comp || NULL == var)
		PSS_COMP_RAISE_RETURN(pss_bytecode_regid_t, comp, "Internal Error: Invalid arguments");

	pss_bytecode_regid_t regid;

	int rc = pss_comp_env_get_var(comp->env, var, 1, &regid);
	if(ERROR_CODE(int) == rc)
		PSS_COMP_RAISE_RETURN(pss_bytecode_regid_t, comp, "Cannot create new variable in current scope");

	return regid;
}

pss_bytecode_regid_t pss_comp_mktmp(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(pss_bytecode_regid_t, comp, "Internal Error: Invalid arguments");

	pss_bytecode_regid_t ret = pss_comp_env_mktmp(comp->env);
	if(ERROR_CODE(pss_bytecode_regid_t) == ret) 
		PSS_COMP_RAISE_RETURN(pss_bytecode_regid_t, comp, "Internal Error: Cannot allocate temp register");

	return ret;
}

int pss_comp_rmtmp(pss_comp_t* comp, pss_bytecode_regid_t regid)
{
	if(NULL == comp || ERROR_CODE(pss_bytecode_regid_t) == regid) 
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	if(ERROR_CODE(int) == pss_comp_env_rmtmp(comp->env, regid))
		PSS_COMP_RAISE_RETURN(pss_bytecode_regid_t, comp, "Internal Error: Cannot remove the temp register");

	return 0;
}

int pss_comp_expect_token(pss_comp_t* comp, pss_comp_lex_token_type_t token)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	const pss_comp_lex_token_t* next = pss_comp_peek(comp, 0);
	if(NULL == next) ERROR_RETURN_LOG(int, "Cannot peek the next token");

	if(next->type != token)
		PSS_COMP_RAISE_RETURN(int, comp, "Syntax Error: Unexpected token");
	
	if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
		ERROR_RETURN_LOG(int, "Cannot consume the last token");

	return 0;
}

int pss_comp_expect_keyword(pss_comp_t* comp, pss_comp_lex_keyword_t keyword)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	const pss_comp_lex_token_t* next = pss_comp_peek(comp, 0);

	if(NULL == next) ERROR_RETURN_LOG(int, "Cannot peek the next token");

	if(next->type != PSS_COMP_LEX_TOKEN_KEYWORD)
		PSS_COMP_RAISE_RETURN(int, comp, "Syntax Error: Keyword expected");


	if(next->value.k != keyword)
		PSS_COMP_RAISE_RETURN(int, comp, "Syntax Error: Unexpected keyword");

	return pss_comp_comsume(comp, 1);
}
