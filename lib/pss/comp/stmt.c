/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/comp/env.h>
#include <pss/comp/lex.h>
#include <pss/comp/comp.h>
#include <pss/comp/block.h>
#include <pss/comp/stmt.h>
#if 0
/**
 * @brief The data structure used to represent a lvalue
 **/
typedef struct {
	enum {
		_LVAL_REG    = 0,                       /*!< The lvalue is a register */
		_LVAL_GLOBAL = 1,                       /*!< The lvalue is a global */
		_LVAL_DICT   = 2                        /*!< The lvalue is an element in the dictionary */
	}                      type;            /*!< The L-Value type */
	pss_bytecode_regid_t   primary;         /*!< The primary addressing register */
	pss_bytecode_regid_t   key;             /*!< The key register */
} _lval_t;

static inline int _free_lval(pss_comp_t* comp)
{
	int rc = 0;
	if(comp->type & _LVAL_GLOBAL && ERROR_CODE(int) == 
}

static inline int _parse_lval(pss_comp_t* comp, int var, _lval_t* result)
{
	pss_comp_env_t* env = NULL;
	pss_bytecode_segment_t* seg = NULL;
	if(NULL == (env = pss_comp_get_env(comp)))
		ERROR_RETURN_LOG(int, "Cannot get the environment for current compiler instance");

	if(NULL == (seg = pss_comp_get_code_segment(comp)))
		ERROR_RETURN_LOG(int, "Cannot get the code segment for current function");
	
	const pss_comp_lex_token_t* token = pss_comp_peek(comp, 0);
	if(token == NULL)
		ERROR_RETURN_LOG(int, "Cannot peek the next token");

	if(token->type != PSS_COMP_LEX_TOKEN_IDENTIFIER)
		PSS_COMP_RAISE_RETURN(int, "Syntax Error: Identifier expected");

	if(var)
	{
		/* If this lvalue is a var decl, it means we have the register option only */

		if(1 != pss_comp_env_get_var(env, token->value.s, 1, &result->primary))
			PSS_COMP_RAISE_RETURN(int, "Syntax Error: Redefined variable '%s'", token[0]->value.s);

		result->key = ERROR_CODE(pss_bytecode_regid_t);
		result->type = _LVAL_REG;

		if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
			ERROR_RETURN_LOG(int, "Cannot consume token");

		return 0;
	}

	/* Otherwise it's a var ref */
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 1);
	int getvar_rc = 2;
	if(strcmp(token->value.s, "$global") != 0 && ERROR_CODE(int) == (getvar_rc = pss_comp_env_get_var(env, token->value.s, 0, &result->primary)))
		PSS_COMP_RAISE_RETURN(int, "Internal Error: Cannot get the variable from the register map");

	if(getvar_rc == 0)
	{
		/* This is a global */
		if(ERROR_CODE(pss_bytecode_regid_t) == (lval->primary = pss_comp_env_mktmp(env)))
			PSS_COMP_RAISE_RETURN(int, "Internal Error: Cannot allocate temp register");

		/* Load the key to the key register */
		if(ERROR_CODE(int) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_STR_LOAD, 
															   PSS_BYTECODE_ARG_STRING(token->value.s), 
															   PSS_BYTECODE_ARG_REGISTER(lval->primary),
															   PSS_BYTECODE_ARG_END))
		{
			pss_comp_env_rmtmp(env, lval->primary);
			PSS_COMP_RAISE_RETURN(int, "Internal Error: Cannot append instruction to the code segment");
		}
		result->type = _LVAL_GLOBAL;
	}
	else if(getvar_rc == 1)
	{
		/* This is a local variable, no need to do anything */
		result->type = _LVAL_REG;
	} else if(getvar_rc == 2)
	{
		/*TODO handle the global accessor */
	}

	if(ahead->type == PSS_COMP_LEX_TOKEN_LBRACE)
	{
		/* TODO: access the subscript */
	}

	return 0;
}

int pss_comp_stmt_assignment(pss_comp_t* comp, int var, pss_comp_stmt_result_t* result)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	pss_comp_env_t* env = pss_comp_get_env(comp);
	if(NULL == env) ERROR_RETURN_LOG(int, "Cannot get compiler abstracted environment");


	/* The lvalue register ID, if it's error code, it means we should use the global storage */
	pss_bytecode_regid_t regid = ERROR_CODE(pss_bytecode_regid_t);

}
#endif
