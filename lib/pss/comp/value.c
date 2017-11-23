/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <error.h>
#include <package_config.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/comp/lex.h>
#include <pss/comp/env.h>
#include <pss/comp/comp.h>
#include <pss/comp/block.h>
#include <pss/comp/value.h>
#include <pss/comp/expr.h>
#include <pss/comp/dict.h>

#define _S(what) PSS_BYTECODE_ARG_STRING(what)

#define _R(what) PSS_BYTECODE_ARG_REGISTER(what)

#define _N(what) PSS_BYTECODE_ARG_NUMERIC(what)

#define _INST(segment, opcode, args...) (ERROR_CODE(pss_bytecode_addr_t) != pss_bytecode_segment_append_code(segment, PSS_BYTECODE_OPCODE_##opcode, ##args, PSS_BYTECODE_ARG_END))

int pss_comp_value_simplify(pss_comp_t* comp, pss_comp_value_t* value)
{
	if(NULL == comp || NULL == value) PSS_COMP_RAISE_INT(comp, ARGS);

	if(value->kind == PSS_COMP_VALUE_KIND_GLOBAL_DICT) PSS_COMP_RAISE_SYN(int, comp, "Malformed global accessor");
	if(value->kind == PSS_COMP_VALUE_KIND_REG) return 0;

	pss_bytecode_segment_t* segment = pss_comp_get_code_segment(comp);
	if(NULL == segment) ERROR_RETURN_LOG(int, "Cannot get current code segment");

	pss_bytecode_regid_t result = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == result)
	    ERROR_RETURN_LOG(int, "Cannot allocate tmp register");

	int nregs;

	if(value->kind == PSS_COMP_VALUE_KIND_DICT)
	{
		nregs = 2;
		if(!_INST(segment, GET_VAL, _R(value->regs[0].id), _R(value->regs[1].id), _R(result)))
		    PSS_COMP_RAISE_INT(comp, CODE);

	}
	else if(value->kind == PSS_COMP_VALUE_KIND_GLOBAL)
	{
		nregs = 1;
		if(!_INST(segment, GLOBAL_GET, _R(value->regs[0].id), _R(result)))
		    PSS_COMP_RAISE_INT(comp, CODE);
	}
	else PSS_COMP_RAISE_INT(comp, BUG);

	if(nregs > 0 && value->regs[0].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[0].id))
	    ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

	if(nregs > 1 && value->regs[1].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[1].id))
	    ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

	value->kind = PSS_COMP_VALUE_KIND_REG;
	value->regs[0].tmp = 1;
	value->regs[0].id  = result;

	return 0;
}

int pss_comp_value_release(pss_comp_t* comp, pss_comp_value_t* value)
{
	if(NULL == comp || NULL == value) PSS_COMP_RAISE_INT(comp, ARGS);

	int n = 1;
	if(value->kind == PSS_COMP_VALUE_KIND_DICT) n = 2;

	if(n > 0 && value->regs[0].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[0].id))
	    ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

	if(n > 1 && value->regs[1].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[1].id))
	    ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

	value->kind = PSS_COMP_VALUE_KIND_REG;
	value->regs[0].tmp = 0;

	return 0;
}
static inline int _make_rvalue(pss_comp_t* comp, pss_comp_value_t* buf)
{
	buf->kind = PSS_COMP_VALUE_KIND_REG;
	if(ERROR_CODE(pss_bytecode_regid_t) == (buf->regs[0].id = pss_comp_mktmp(comp)))
	    ERROR_RETURN_LOG(int, "Cannot make tmp register for the string constant");
	buf->regs[0].tmp = 1;
	return 0;
}

static inline int _parse_function_literal(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_comp_value_t* buf)
{
	const pss_comp_lex_token_t* ahead = NULL;
	char* func_name = NULL;
	uint32_t argc = 0;
	char* argv[128] = {};

	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_FUNCTION)) return ERROR_CODE(int);

	if(NULL == (ahead = pss_comp_peek(comp, 0))) return ERROR_CODE(int);
	if(ahead->type == PSS_COMP_LEX_TOKEN_IDENTIFIER)
	{
		if(NULL == (func_name = strdup(ahead->value.s)))
		{
			pss_comp_raise_internal(comp, PSS_COMP_INTERNAL_MALLOC);
			goto FUNC_ERR;
		}
		if(ERROR_CODE(int) == pss_comp_consume(comp, 1)) ERROR_LOG_GOTO(FUNC_ERR, "Cannot consume the ahead token");
	}

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LPARENTHESIS)) return ERROR_CODE(int);

	for(;argc < sizeof(argv) / sizeof(argv[0]);)
	{
		if(NULL == (ahead = pss_comp_peek(comp, 0)))
		    ERROR_LOG_GOTO(FUNC_ERR, "Cannot peek the next token");
		if(ahead->type == PSS_COMP_LEX_TOKEN_RPARENTHESIS) break;
		else if(ahead->type != PSS_COMP_LEX_TOKEN_IDENTIFIER)
		    PSS_COMP_RAISE_SYN_GOTO(FUNC_ERR, comp, "Invalid argument list, identifier expected");

		size_t len = strlen(ahead->value.s);
		if(NULL == (argv[argc] = (char*)malloc(len + 1)))
		    PSS_COMP_RAISE_INT_GOTO(FUNC_ERR, comp, MALLOC);

		memcpy(argv[argc], ahead->value.s, len + 1);
		argc ++;

		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
		    ERROR_LOG_GOTO(FUNC_ERR, "Cannot consume the ahead token");

		if(NULL == (ahead = pss_comp_peek(comp, 0)))
		    ERROR_LOG_GOTO(FUNC_ERR, "Cannot peek the next token");

		if(ahead->type == PSS_COMP_LEX_TOKEN_COMMA)
		{
			if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			    ERROR_LOG_GOTO(FUNC_ERR, "Cannot consume the argument seperator");
		}
		else if(ahead->type != PSS_COMP_LEX_TOKEN_RPARENTHESIS)
		    PSS_COMP_RAISE_SYN_GOTO(FUNC_ERR, comp, "Unexpected token in the function argument list");
	}

	if(argc >= sizeof(argv) / sizeof(argv[0]))
	    PSS_COMP_RAISE_SYN_GOTO(FUNC_ERR, comp, "Too many arguments in the function argument list");

	if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
	    ERROR_LOG_GOTO(FUNC_ERR, "Cannot consume the end of argument list");

	if(ERROR_CODE(int) == pss_comp_open_closure(comp, func_name, argc, (char const* const* const)argv))
	    ERROR_LOG_GOTO(FUNC_ERR, "Cannot open the closure for the nested function");

	if(NULL != func_name) free(func_name);
	func_name = NULL;

	for(; argc > 0; argc --)
	    free(argv[argc - 1]);

	if(ERROR_CODE(int) == pss_comp_block_parse(comp, PSS_COMP_LEX_TOKEN_LBRACE, PSS_COMP_LEX_TOKEN_RBRACE, 0))
	    ERROR_RETURN_LOG(int, "Cannot parse the function body");

	pss_bytecode_segid_t funcid = pss_comp_close_closure(comp);
	if(ERROR_CODE(pss_bytecode_segid_t) == funcid)
	    ERROR_RETURN_LOG(int, "Cannot put the closure body to the module");

	if(ERROR_CODE(int) == _make_rvalue(comp, buf)) return ERROR_CODE(int);

	if(!_INST(seg, INT_LOAD, _N(funcid), _R(buf->regs[0].id)) ||
	   !_INST(seg, CLOSURE_NEW, _R(buf->regs[0].id), _R(buf->regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);

	return 0;
FUNC_ERR:
	if(NULL != func_name) free(func_name);
	for(;argc > 0; argc --)
	    free(argv[argc-1]);
	return ERROR_CODE(int);
}

static inline int _parse_variable_term(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_comp_value_t* buf)
{
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot ppek token");

	if(strcmp(ahead->value.s, "$global") == 0)
	{
		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
		    ERROR_RETURN_LOG(int, "Cannot consume token");
		buf->kind = PSS_COMP_VALUE_KIND_GLOBAL_DICT;
		return 0;
	}

	/* Then we need to look up the variable */
	int lookup_rc = pss_comp_get_local_var(comp, ahead->value.s, &buf->regs[0].id);
	if(ERROR_CODE(int) == lookup_rc)
	    ERROR_RETURN_LOG(int, "Cannot look up the variable table");

	if(lookup_rc == 0)
	{
		/* This is a global variable */
		buf->kind = PSS_COMP_VALUE_KIND_GLOBAL;
		if(ERROR_CODE(pss_bytecode_regid_t) == (buf->regs[0].id = pss_comp_mktmp(comp)))
		    ERROR_RETURN_LOG(int, "Cannot make the temp variable");
		else
		    buf->regs[0].tmp = 1;

		if(!_INST(seg, STR_LOAD, _S(ahead->value.s), _R(buf->regs[0].id)))
		    PSS_COMP_RAISE_INT(comp, CODE);
	}
	else
	{
		buf->kind = PSS_COMP_VALUE_KIND_REG;
		buf->regs[0].tmp = 0;
	}

	if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
	    ERROR_RETURN_LOG(int, "Cannot consume the last token");

	return 0;
}

static inline int _parse_literal(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_comp_value_t* buf)
{
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek token");

	if(ERROR_CODE(int) == _make_rvalue(comp, buf)) return ERROR_CODE(int);

	if(ahead->type == PSS_COMP_LEX_TOKEN_STRING && !_INST(seg, STR_LOAD, _S(ahead->value.s), _R(buf->regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);
	if(ahead->type == PSS_COMP_LEX_TOKEN_INTEGER && !_INST(seg, INT_LOAD, _N(ahead->value.i), _R(buf->regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);
	if(ahead->type == PSS_COMP_LEX_TOKEN_KEYWORD && ahead->value.k == PSS_COMP_LEX_KEYWORD_UNDEFINED && !_INST(seg, UNDEF_LOAD, _R(buf->regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
	    ERROR_RETURN_LOG(int, "Cannot consume the string literal");

	return 0;
}

static inline int _parse_unary(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_comp_value_t* buf)
{
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek token");

	int not = 0;

	if(ahead->type == PSS_COMP_LEX_TOKEN_NOT) not = 1;

	if(ERROR_CODE(int) == pss_comp_consume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
	if(ERROR_CODE(int) == pss_comp_value_parse(comp, buf)) ERROR_RETURN_LOG(int, "Cannot parse the inner register");
	if(ERROR_CODE(int) == pss_comp_value_simplify(comp, buf))  ERROR_RETURN_LOG(int, "Cannot construct the R-Value");

	pss_comp_value_t val = {};
	if(ERROR_CODE(int) == _make_rvalue(comp, &val)) ERROR_RETURN_LOG(int, "Cannot make the temp value");

	if(!_INST(seg, INT_LOAD, _N(0), _R(val.regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);
	if(not && !_INST(seg, EQ, _R(val.regs[0].id), _R(buf->regs[0].id), _R(val.regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);
	if(!not && !_INST(seg, SUB, _R(val.regs[0].id), _R(buf->regs[0].id), _R(val.regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_value_release(comp, buf))
	    ERROR_RETURN_LOG(int, "Cannot release the tmp value");

	*buf = val;

	return 0;
}

static inline int _parse_inc_dec(pss_comp_t* comp, pss_bytecode_segment_t* seg, const pss_comp_value_t* prefix, pss_comp_value_t* buf)
{
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek the ahead token");
	int inc = (ahead->type == PSS_COMP_LEX_TOKEN_INCREASE);

	if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
	    ERROR_RETURN_LOG(int, "Cannot consume token");

	pss_comp_value_t term = {};
	if(prefix == NULL)
	{
		if(ERROR_CODE(int) == pss_comp_value_parse(comp, &term))
		    ERROR_RETURN_LOG(int, "Cannot parse the actual term");
	}
	else term = *prefix;

	if(!pss_comp_value_is_lvalue(&term))
	    PSS_COMP_RAISE_SYN(int, comp, "L-Value expected");

	pss_comp_value_t one = {};
	if(ERROR_CODE(int) == _make_rvalue(comp, &one))
	    ERROR_RETURN_LOG(int, "Cannot allocate temp register");

	if(!_INST(seg, INT_LOAD, _N(1), _R(one.regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);

	pss_comp_value_t tmp = {};
	if(ERROR_CODE(int) == _make_rvalue(comp, &tmp))
	    ERROR_RETURN_LOG(int, "Cannot allocate temp register");


	switch(term.kind)
	{
		case PSS_COMP_VALUE_KIND_REG:
		    if(!_INST(seg, MOVE, _R(term.regs[0].id), _R(tmp.regs[0].id)))
		        PSS_COMP_RAISE_INT(comp, CODE);
		    break;
		case PSS_COMP_VALUE_KIND_GLOBAL:
		    if(!_INST(seg, GLOBAL_GET, _R(term.regs[0].id), _R(tmp.regs[0].id)))
		        PSS_COMP_RAISE_INT(comp, CODE);
		    break;
		case PSS_COMP_VALUE_KIND_DICT:
		    if(!_INST(seg, GET_VAL, _R(term.regs[0].id), _R(term.regs[1].id), _R(tmp.regs[0].id)))
		        PSS_COMP_RAISE_INT(comp, CODE);
		    break;
		default:
		    PSS_COMP_RAISE_INT(comp, BUG);
	}

	pss_comp_value_t saved = {};
	if(NULL != prefix)
	{
		if(ERROR_CODE(int) == _make_rvalue(comp, &saved))
		    ERROR_RETURN_LOG(int, "Cannot allocate tmp register");
		if(!_INST(seg, MOVE, _R(tmp.regs[0].id), _R(saved.regs[0].id)))
		    PSS_COMP_RAISE_INT(comp, CODE);
	}

	if((inc && !_INST(seg, ADD, _R(one.regs[0].id), _R(tmp.regs[0].id), _R(tmp.regs[0].id))) ||
	   (!inc && !_INST(seg, SUB, _R(tmp.regs[0].id), _R(one.regs[0].id), _R(tmp.regs[0].id))))
	    PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &one))
	    ERROR_RETURN_LOG(int, "Cannot release the value");

	switch(term.kind)
	{
		case PSS_COMP_VALUE_KIND_REG:
		    if(!_INST(seg, MOVE, _R(tmp.regs[0].id), _R(term.regs[0].id)))
		        PSS_COMP_RAISE_INT(comp, CODE);
		    break;
		case PSS_COMP_VALUE_KIND_GLOBAL:
		    if(!_INST(seg, GLOBAL_SET, _R(tmp.regs[0].id), _R(term.regs[0].id)))
		        PSS_COMP_RAISE_INT(comp, CODE);
		    break;
		case PSS_COMP_VALUE_KIND_DICT:
		    if(!_INST(seg, SET_VAL, _R(tmp.regs[0].id), _R(term.regs[0].id), _R(term.regs[1].id)))
		        PSS_COMP_RAISE_INT(comp, CODE);
		    break;
		default:
		    PSS_COMP_RAISE_INT(comp, BUG);
	}

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &term))
	    ERROR_RETURN_LOG(int, "Cannot release the value");


	if(NULL == prefix)
	    *buf = tmp;
	else
	{
		if(ERROR_CODE(int) == pss_comp_value_release(comp, &tmp))
		    ERROR_RETURN_LOG(int, "Cannot release the tmp value");
		*buf = saved;
	}

	return 0;
}

static inline int _parse_subscript(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_comp_value_t* buf)
{
	(void)seg;
	if(pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LBRACKET) == ERROR_CODE(int)) return ERROR_CODE(int);

	if(buf->kind != PSS_COMP_VALUE_KIND_GLOBAL_DICT)
	{
		if(ERROR_CODE(int) == pss_comp_value_simplify(comp, buf))
		    ERROR_RETURN_LOG(int, "Cannot simplify the primtive value");

		pss_comp_value_t val = {};
		if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		    ERROR_RETURN_LOG(int, "Cannot parse the subscript");

		if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
		    ERROR_RETURN_LOG(int, "Cannot simplify the value");

		buf->kind = PSS_COMP_VALUE_KIND_DICT;
		buf->regs[1] = val.regs[0];
	}
	else
	{
		pss_comp_value_t val = {};
		if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		    ERROR_RETURN_LOG(int, "Cannot parse the subscript");

		if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
		    ERROR_RETURN_LOG(int, "Cannot simplify the value");

		buf->kind = PSS_COMP_VALUE_KIND_GLOBAL;
		buf->regs[0] = val.regs[0];
	}

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RBRACKET)) return ERROR_CODE(int);

	return 0;
}

static inline int _parse_application(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_comp_value_t* buf)
{
	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LPARENTHESIS))
	    return ERROR_CODE(int);

	if(ERROR_CODE(int) == pss_comp_value_simplify(comp, buf))
	    ERROR_RETURN_LOG(int, "Cannot simplify the primtive value");

	const pss_comp_lex_token_t* ahead = NULL;

	if(NULL == (ahead = pss_comp_peek(comp, 0)))
	    ERROR_RETURN_LOG(int, "Cannot peek the token");

	uint32_t i = 0;

	pss_comp_value_t args[PSS_VM_ARG_MAX] = {};

	for(;i < PSS_VM_ARG_MAX;i++)
	{
		if(ahead->type == PSS_COMP_LEX_TOKEN_RPARENTHESIS) break;

		if(ERROR_CODE(int) == pss_comp_expr_parse(comp, args + i))
		    ERROR_RETURN_LOG(int, "Cannot parse argument");

		if(ERROR_CODE(int) == pss_comp_value_simplify(comp, args + i))
		    ERROR_RETURN_LOG(int, "Cannot simplify the argument");

		if(NULL == (ahead = pss_comp_peek(comp, 0)))
		    ERROR_RETURN_LOG(int, "Cannot peek the token");

		if(ahead->type == PSS_COMP_LEX_TOKEN_RPARENTHESIS) break;
		else if(ahead->type == PSS_COMP_LEX_TOKEN_COMMA)
		{
			if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			    ERROR_RETURN_LOG(int, "Cannot consume the token");
			if(NULL == (ahead = pss_comp_peek(comp, 0)))
			    ERROR_RETURN_LOG(int, "Cannot peek the next token");
		}
		else PSS_COMP_RAISE_SYN(int, comp, "Invalid argument list");
	}

	uint32_t j;
	for(j = 0; j <= i && j < PSS_VM_ARG_MAX; j ++)
	    if(!_INST(seg, ARG, _R(args[j].regs[0].id)))
	        PSS_COMP_RAISE_INT(comp, CODE);

	if(i >= PSS_VM_ARG_MAX) PSS_COMP_RAISE_SYN(int, comp, "Too many arguments in the function application");

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RPARENTHESIS))
	    return ERROR_CODE(int);

	pss_comp_value_t tmp = {};
	if(ERROR_CODE(int) == _make_rvalue(comp, &tmp))
	    return ERROR_CODE(int);

	if(!_INST(seg, CALL, _R(buf->regs[0].id), _R(tmp.regs[0].id)))
	    PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_value_release(comp, buf))
	    ERROR_RETURN_LOG(int, "Cannot release the argument");

	*buf = tmp;

	for(; i > 0; i--)
	    if(ERROR_CODE(int) == pss_comp_value_release(comp, args + i))
	        ERROR_RETURN_LOG(int, "Cannot release the argument");


	return 0;
}

static inline int _parse_trailer(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_comp_value_t* buf)
{
	const pss_comp_lex_token_t* ahead = NULL;
	for(;;)
	{
		if(NULL == (ahead = pss_comp_peek(comp, 0)))
		    ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

		if(ahead->type == PSS_COMP_LEX_TOKEN_LBRACKET)
		{
			if(ERROR_CODE(int) == _parse_subscript(comp, seg, buf))
			    return ERROR_CODE(int);
		}
		else if(ahead->type == PSS_COMP_LEX_TOKEN_LPARENTHESIS)
		{
			if(ERROR_CODE(int) == _parse_application(comp, seg, buf))
			    return ERROR_CODE(int);
		}
		else if(ahead->type == PSS_COMP_LEX_TOKEN_INCREASE || ahead->type == PSS_COMP_LEX_TOKEN_DECREASE)
		{
			pss_comp_value_t val = *buf;
			/* var ++ */
			if(ERROR_CODE(int) == _parse_inc_dec(comp, seg, &val, buf))
			    return ERROR_CODE(int);
		}
		else break;
	}

	return 0;
}


int pss_comp_value_parse(pss_comp_t* comp, pss_comp_value_t* buf)
{
	if(NULL == comp || NULL == buf)
	    PSS_COMP_RAISE_INT(comp, CODE);

	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek token");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get the bytecode segment");
	int rc = ERROR_CODE(int);

	switch(ahead->type)
	{
		case PSS_COMP_LEX_TOKEN_LPARENTHESIS:
		    if(ERROR_CODE(int) == pss_comp_consume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
		    if(ERROR_CODE(int) == pss_comp_expr_parse(comp, buf)) ERROR_RETURN_LOG(int, "Cannot parse the rvalue primitive");
		    rc = pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RPARENTHESIS);
		    break;

		case PSS_COMP_LEX_TOKEN_STRING:
		case PSS_COMP_LEX_TOKEN_INTEGER:
		    rc = _parse_literal(comp, seg, buf);
		    break;

		case PSS_COMP_LEX_TOKEN_KEYWORD:
		    if(ahead->value.k == PSS_COMP_LEX_KEYWORD_UNDEFINED)
		        rc = _parse_literal(comp, seg, buf);
		    else if(ahead->value.k == PSS_COMP_LEX_KEYWORD_FUNCTION)
		        rc = _parse_function_literal(comp, seg, buf);
		    else PSS_COMP_RAISE_SYN(int, comp, "Unexpected keyword while parsing value");

		    break;

		case PSS_COMP_LEX_TOKEN_MINUS:
		case PSS_COMP_LEX_TOKEN_NOT:
		    rc = _parse_unary(comp, seg, buf);
		    break;

		case PSS_COMP_LEX_TOKEN_LBRACE:
		    rc = pss_comp_dict_parse(comp, buf);
		    break;

		case PSS_COMP_LEX_TOKEN_IDENTIFIER:
		    rc = _parse_variable_term(comp, seg, buf);
		    break;

		case PSS_COMP_LEX_TOKEN_INCREASE:
		case PSS_COMP_LEX_TOKEN_DECREASE:
		    /* ++var or --var */
		    rc = _parse_inc_dec(comp, seg, NULL, buf);
		    break;

		default:
		    PSS_COMP_RAISE_SYN(int, comp, "Invalid value term");
	}

	if(ERROR_CODE(int) == rc) ERROR_RETURN_LOG(int, "Cannot parse simple primitive");

	return _parse_trailer(comp, seg, buf);
}
