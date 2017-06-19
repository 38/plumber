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

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/comp/lex.h>
#include <pss/comp/comp.h>
#include <pss/comp/env.h>

#define _LOOKAHEAD 3

struct _pss_comp_t {
	pss_comp_lex_t*          lexer;            /*!< The lexer we are using */
	pss_bytecode_module_t*   module;           /*!< The output bytecode module */
	pss_comp_env_t*          env;              /*!< The compile time environment abstraction */
	pss_comp_error_t**       error_buf;        /*!< The last compiler error */
	pss_comp_lex_token_t     ahead[_LOOKAHEAD];/*!< The lookahead ring buffer */
	uint32_t                 ahead_begin;      /*!< The ring buffer begin */
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
			if(pss_comp_lex_next_token(comp->lexer, comp->ahead + off) == ERROR_CODE(int))\
				PSS_COMP_RAISE(ERR, comp, INTERNAL, "Cannot peek token");
		}
	}
	return comp->ahead + (n + comp->ahead_begin) % _LOOKAHEAD;
ERR:
	return NULL;
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

pss_bytecode_module_t* pss_comp_output(pss_comp_t* comp)
{
	if(NULL == comp) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return comp->module;
}




