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
#include <pss/comp/comp.h>
#include <pss/comp/env.h>

#include <pss/comp/block.h>

#define _LOOKAHEAD 3

struct _pss_comp_t {
	pss_comp_lex_t*          lexer;            /*!< The lexer we are using */
	pss_bytecode_module_t*   module;           /*!< The output bytecode module */
	pss_comp_env_t*          env;              /*!< The compile time environment abstraction */
	pss_comp_error_t**       error_buf;        /*!< The last compiler error */
	pss_comp_lex_token_t     ahead[_LOOKAHEAD];/*!< The lookahead ring buffer */
	uint32_t                 ahead_begin;      /*!< The ring buffer begin */
	pss_bytecode_segment_t*  seg_stack[PSS_COMP_ENV_SCOPE_MAX];  /*!< The current code segment the compiler is writting */
	uint32_t                 seg_stack_top;    /*!< The stack top for the current segment ID */
};

int pss_comp_compile(pss_comp_option_t* option, pss_comp_error_t** error)
{
	if(NULL == option || NULL == option->lexer || NULL == option->module)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_comp_t compiler = {
		.lexer = option->lexer,
		.module = option->module,
		.error_buf = error,
		.ahead_begin = 0
	};

	if(NULL == (compiler.env = pss_comp_env_new()))
	{
		pss_comp_raise(&compiler, "Internal error");
		ERROR_RETURN_LOG(int, "Cannot create new environment");
	}

	/* TODO Compilation */

	return 0;
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
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	uint32_t i;
	for(i = 0; i <= n; i ++)
	{
		uint32_t off = (i + comp->ahead_begin) % _LOOKAHEAD;
		if(comp->ahead[off].type == PSS_COMP_LEX_TOKEN_NAT)
		{
			if(pss_comp_lex_next_token(comp->lexer, comp->ahead + off) == ERROR_CODE(int))
				PSS_COMP_RAISE_INTERNAL_PTR(comp, "Cannot peek lexer token");
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
	if(NULL == comp) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(comp->seg_stack_top == 0) 
		PSS_COMP_RAISE_INTERNAL_PTR(comp, "Compiler is curarently out of closure");

	return comp->seg_stack[comp->seg_stack_top - 1];
}

int pss_comp_open_closure(pss_comp_t* comp, uint32_t nargs, char const** argnames)
{
	if(NULL == comp || ERROR_CODE(uint32_t) == nargs || NULL == argnames)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == pss_comp_env_open_scope(comp->env))
		PSS_COMP_RAISE_INTERNAL(int, comp, "Cannot open new scope for closure");

	pss_bytecode_regid_t argid[nargs];
	pss_bytecode_segment_t* segment = NULL;
	uint32_t i;
	for(i = 0; i < nargs; i ++)
		if(1 != pss_comp_env_get_var(comp->env, argnames[i], 1, argid + i))
			PSS_COMP_RAISE_INTERAL_GOTO(ERR, comp, "Cannot allocate register for the argument");
			
	if(NULL == (segment = pss_bytecode_segment_new((pss_bytecode_regid_t)nargs, argid)))
		PSS_COMP_RAISE_INTERAL_GOTO(ERR, comp, "Cannot create new code segment for the segment");

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
	if(NULL == comp) ERROR_RETURN_LOG(pss_bytecode_segid_t, "Invalid arguments");
	if(comp->seg_stack_top == 0)
		PSS_COMP_RAISE_INTERNAL(pss_bytecode_segid_t, comp, "Compiler is current out of closure");

	pss_bytecode_segid_t ret = pss_bytecode_module_append(comp->module, comp->seg_stack[--comp->seg_stack_top]);
	if(ERROR_CODE(pss_bytecode_segid_t) == ret)
		PSS_COMP_RAISE_INTERNAL(pss_bytecode_segid_t, comp, "Cannot put the segment to the bytecode module");

	return ret;
}

