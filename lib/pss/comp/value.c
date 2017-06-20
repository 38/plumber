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

int pss_comp_value_simplify(pss_comp_t* comp, pss_comp_value_t* value)
{
	if(NULL == comp || NULL == value) PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	if(value->kind == PSS_COMP_VALUE_KIND_REG) return 0;

	pss_bytecode_segment_t* segment = pss_comp_get_code_segment(comp);
	if(NULL == segment)
		ERROR_RETURN_LOG(int, "Cannot get current code segment");

	pss_bytecode_regid_t result = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == result)
		ERROR_RETURN_LOG(int, "Cannot allocate tmp register");

	int nresgs;

	if(value->kind == PSS_COMP_VALUE_KIND_DICT)
	{
		nresgs = 2;
		if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(segment, 
			PSS_BYTECODE_OPCODE_GET_VAL,
			PSS_BYTECODE_ARG_REGISTER(value->regs[0].id),
			PSS_BYTECODE_ARG_REGISTER(value->regs[1].id),
			PSS_BYTECODE_ARG_REGISTER(result),
			PSS_BYTECODE_ARG_END))
			PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot append instruction to code segment");

	}
	else
	{
		nresgs = 1;
		if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(segment,
			PSS_BYTECODE_OPCODE_GLOBAL_GET,
			PSS_BYTECODE_ARG_REGISTER(value->regs[0].id),
			PSS_BYTECODE_ARG_REGISTER(result),
			PSS_BYTECODE_ARG_END))
			PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot append instruction to code segment");
	}

	if(nresgs > 0 && value->regs[0].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[0].id))
		ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

	if(nresgs > 1 && value->regs[1].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[1].id))
		ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

	value->kind = PSS_COMP_VALUE_KIND_REG;
	value->regs[0].tmp = 1;
	value->regs[1].id  = result;

	return 0;
}

int pss_comp_value_release(pss_comp_t* comp, pss_comp_value_t* value)
{
	if(NULL == comp || NULL == value)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	int n = 1;
	if(value->kind == PSS_COMP_VALUE_KIND_DICT) n = 2;

	if(n > 0 && value->regs[0].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[0].id))
		ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

	if(n > 1 && value->regs[1].tmp && ERROR_CODE(int) == pss_comp_rmtmp(comp, value->regs[1].id))
		ERROR_RETURN_LOG(int, "Cannot release the tmp variable");

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
	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_FUNCTION)) return ERROR_CODE(int);
	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LPARENTHESIS)) return ERROR_CODE(int);
	uint32_t argc = 0;
	char* argv[128] = {};
	const pss_comp_lex_token_t* ahead = NULL;
	for(;argc < sizeof(argv) / sizeof(argv[0]);)
	{
		if(NULL == (ahead = pss_comp_peek(comp, 0)))
			ERROR_LOG_GOTO(FUNC_ERR, "Cannot peek the next token");
		if(ahead->type == PSS_COMP_LEX_TOKEN_RPARENTHESIS) break;
		else if(ahead->type != PSS_COMP_LEX_TOKEN_IDENTIFIER)
			PSS_COMP_RAISE_GOTO(FUNC_ERR, comp, "Syntax error: Invalid argument list");

		size_t len = strlen(ahead->value.s);
		if(NULL == (argv[argc] = (char*)malloc(len + 1)))
			PSS_COMP_RAISE_GOTO(FUNC_ERR, comp, "Internal Error: Cannot allocate memory for the argument list");

		memcpy(argv[argc], ahead->value.s, len + 1);

		argc ++;

		if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
			PSS_COMP_RAISE_GOTO(FUNC_ERR, comp, "Internal Error: Cannot consume the token");

		if(NULL == (ahead = pss_comp_peek(comp, 0)))
			ERROR_LOG_GOTO(FUNC_ERR, "Cannot peek the next token");

		if(ahead->type == PSS_COMP_LEX_TOKEN_COMMA)
		{
			if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
				ERROR_LOG_GOTO(FUNC_ERR, "Cannot consume the argument seperator");
		}
		else if(ahead->type != PSS_COMP_LEX_TOKEN_RPARENTHESIS)
			PSS_COMP_RAISE_GOTO(FUNC_ERR, comp, "Syntax error: Unexpected token in argument list");
	}

	if(argc >= sizeof(argv) / sizeof(argv[0]))
		PSS_COMP_RAISE_GOTO(FUNC_ERR, comp, "Syntax error: too many argument in function param list");

	if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
		ERROR_LOG_GOTO(FUNC_ERR, "Cannot consume the end of argument list");

	if(ERROR_CODE(int) == pss_comp_open_closure(comp, argc, (const char**)argv))
		ERROR_LOG_GOTO(FUNC_ERR, "Cannot open the closure for the nested function");

	for(; argc > 0; argc --)
		free(argv[argc - 1]);

	pss_comp_block_t body_result;

	if(ERROR_CODE(int) == pss_comp_block_parse(comp, PSS_COMP_LEX_TOKEN_LBRACE, PSS_COMP_LEX_TOKEN_RBRACE, &body_result))
		ERROR_RETURN_LOG(int, "Cannot parse the function body");

	pss_bytecode_segid_t funcid = pss_comp_close_closure(comp);
	if(ERROR_CODE(pss_bytecode_segid_t) == funcid)
		ERROR_RETURN_LOG(int, "Cannot put the closure body to the module");

	if(ERROR_CODE(int) == _make_rvalue(comp, buf)) return ERROR_CODE(int);

	if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_INT_LOAD, 
				                                                PSS_BYTECODE_ARG_NUMERIC(funcid), 
																PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id), 
																PSS_BYTECODE_ARG_END))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot load the function id to the register");
	if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_CLOSURE_NEW, 
				                                                PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id), 
																PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id), 
																PSS_BYTECODE_ARG_END))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Cannot load the closure-new instruction");

FUNC_ERR:
	for(;argc > 0; argc --)
		free(argv[argc-1]);
	return ERROR_CODE(int);
}


static inline int _parse_primitive(pss_comp_t* comp, pss_comp_value_t* buf)
{
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek token");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get the bytecode segment");

	switch(ahead->type)
	{
		case PSS_COMP_LEX_TOKEN_LPARENTHESIS:
			/* This is a (....) */
			if(ERROR_CODE(int) == pss_comp_comsume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
			int rc = pss_comp_value_parse(comp, buf);
			if(ERROR_CODE(int) == rc) ERROR_RETURN_LOG(int, "Cannot parse the rvalue primitive");
			return pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RPARENTHESIS);
		case PSS_COMP_LEX_TOKEN_STRING:
			if(ERROR_CODE(int) == _make_rvalue(comp, buf)) return ERROR_CODE(int);
			if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_STR_LOAD, 
						                                                PSS_BYTECODE_ARG_STRING(ahead->value.s), 
																		PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id),
																		PSS_BYTECODE_ARG_END))
				PSS_COMP_RAISE_RETURN(int, comp, "Cannot append string load instruction to the bytecode table");
			if(ERROR_CODE(int) == pss_comp_comsume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
			return 0;
		case PSS_COMP_LEX_TOKEN_INTEGER:
			if(ERROR_CODE(int) == _make_rvalue(comp, buf)) return ERROR_CODE(int);
			if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_INT_LOAD,
						                                                PSS_BYTECODE_ARG_NUMERIC(ahead->value.i),
																		PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id),
																		PSS_BYTECODE_ARG_END))
				PSS_COMP_RAISE_RETURN(int, comp, "Cannot append integer load instruction to the bytecode table");
			if(ERROR_CODE(int) == pss_comp_comsume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
			return 0;
		case PSS_COMP_LEX_TOKEN_KEYWORD:
			if(ahead->value.k == PSS_COMP_LEX_KEYWORD_UNDEFINED)
			{
				if(ERROR_CODE(int) == pss_comp_comsume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
				if(ERROR_CODE(int) == _make_rvalue(comp, buf)) return ERROR_CODE(int);
				if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_UNDEF_LOAD,
							                                                PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id),
																			PSS_BYTECODE_ARG_END))
					PSS_COMP_RAISE_RETURN(int, comp, "Cannot append undefined load instruction to the bytecode table");
				return 0;
			} 
			else if(ahead->value.k == PSS_COMP_LEX_KEYWORD_FUNCTION)
				return _parse_function_literal(comp, seg, buf);
		case PSS_COMP_LEX_TOKEN_MINUS:
			if(ERROR_CODE(int) == pss_comp_comsume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
			if(ERROR_CODE(int) == _parse_primitive(comp, buf)) ERROR_RETURN_LOG(int, "Cannot parse the inner register");
			if(ERROR_CODE(int) == pss_comp_value_simplify(comp, buf))  ERROR_RETURN_LOG(int, "Cannot construct the R-Value");
			{
				pss_comp_value_t val = {};
				if(ERROR_CODE(int) == _make_rvalue(comp, &val))
					ERROR_RETURN_LOG(int, "Cannot make the temp value");
				if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_UNDEF_LOAD,
																			PSS_BYTECODE_ARG_NUMERIC(0),
																			PSS_BYTECODE_ARG_REGISTER(val.regs[0].id),
																			PSS_BYTECODE_ARG_END))
					PSS_COMP_RAISE_RETURN(int, comp, "Cannot append int-load instruction");
				if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_SUB,
							                                                PSS_BYTECODE_ARG_REGISTER(val.regs[0].id),
																			PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id),
																			PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id),
																			PSS_BYTECODE_ARG_END))
					PSS_COMP_RAISE_RETURN(int, comp, "Cannot append sub instruction");
				if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
					ERROR_RETURN_LOG(int, "Cannot release the tmp value");
				return 0;
			}
		case PSS_COMP_LEX_TOKEN_NOT:
			if(ERROR_CODE(int) == pss_comp_comsume(comp, 1)) ERROR_RETURN_LOG(int, "Cannot consume the first token");
			if(ERROR_CODE(int) == _parse_primitive(comp, buf)) ERROR_RETURN_LOG(int, "Cannot parse the inner register");
			if(ERROR_CODE(int) == pss_comp_value_simplify(comp, buf))  ERROR_RETURN_LOG(int, "Cannot construct the R-Value");
			{
				pss_comp_value_t val = {};
				if(ERROR_CODE(int) == _make_rvalue(comp, &val))
					ERROR_RETURN_LOG(int, "Cannot make the temp value");
				if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_UNDEF_LOAD,
																			PSS_BYTECODE_ARG_NUMERIC(0),
																			PSS_BYTECODE_ARG_REGISTER(val.regs[0].id),
																			PSS_BYTECODE_ARG_END))
					PSS_COMP_RAISE_RETURN(int, comp, "Cannot append int-load instruction");
				if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_EQ,
							                                                PSS_BYTECODE_ARG_REGISTER(val.regs[0].id),
																			PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id),
																			PSS_BYTECODE_ARG_REGISTER(buf->regs[0].id),
																			PSS_BYTECODE_ARG_END))
					PSS_COMP_RAISE_RETURN(int, comp, "Cannot append sub instruction");
				if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
					ERROR_RETURN_LOG(int, "Cannot release the tmp value");
				return 0;
			}
		case PSS_COMP_LEX_TOKEN_LBRACE: /*TODO*/ break;
		case PSS_COMP_LEX_TOKEN_IDENTIFIER:
			/*TODO parse either the variable or function call */
		default:
			break;
	}
	

	PSS_COMP_RAISE_RETURN(int, comp, "Syntax error: Invalid primitive value");
}
#if 0
int pss_comp_value_parse(pss_comp_t* comp, pss_comp_value_t* result)
{
	if(NULL == comp || NULL == result)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");


}
#endif
