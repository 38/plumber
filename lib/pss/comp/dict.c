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

int pss_comp_dict_parse(pss_comp_t* comp, pss_comp_value_t* buf)
{
	if(NULL == comp || NULL == buf)
		PSS_COMP_RAISE_INT(comp, CODE);

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get the bytecode segment");

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LBRACE))
		PSS_COMP_RAISE_SYN(int, comp, "Left parenthesis expected in a dict/service literal");

	buf->kind = PSS_COMP_VALUE_KIND_REG;
	if(ERROR_CODE(pss_bytecode_regid_t) == (buf->regs[0].id = pss_comp_mktmp(comp)))
		ERROR_RETURN_LOG(int, "Cannot create the dictionary register");
	buf->regs[0].tmp = 1;

	if(!_INST(seg, DICT_NEW, _R(buf->regs[0].id)))
		PSS_COMP_RAISE_INT(comp, CODE);

	for(;;)
	{
		const pss_comp_lex_token_t* ahead[2] = {pss_comp_peek(comp, 0), pss_comp_peek(comp, 1)};
		if(NULL == ahead[0] || NULL == ahead[1])
			ERROR_RETURN_LOG(int, "Cannot peek the token ahead");

		if(ahead[1]->type == PSS_COMP_LEX_TOKEN_COLON_EQUAL ||
		   ahead[1]->type == PSS_COMP_LEX_TOKEN_COLON)
		{
			/* This is a key-value literal */
			if(ahead[0]->type != PSS_COMP_LEX_TOKEN_STRING && ahead[0]->type != PSS_COMP_LEX_TOKEN_IDENTIFIER)
				PSS_COMP_RAISE_SYN(int, comp, "Unexpected token in key-value literal");

			char key[1024];
			strcpy(key, ahead[0]->value.s);

			if(ERROR_CODE(int) == pss_comp_consume(comp, 2))
				ERROR_RETURN_LOG(int, "Cannot consume ahead token");
			
			pss_comp_value_t val;
			if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
				ERROR_RETURN_LOG(int, "Cannot parse the value expression");

			if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
				ERROR_RETURN_LOG(int, "Cannot simplify the value");

			pss_bytecode_regid_t key_reg = pss_comp_mktmp(comp);
			if(ERROR_CODE(pss_bytecode_regid_t) == key_reg) 
				ERROR_RETURN_LOG(int, "Cannot allocate register for the key");

			if(!_INST(seg, STR_LOAD, _S(key), _R(key_reg)))
				PSS_COMP_RAISE_INT(comp, CODE);

			if(!_INST(seg, SET_VAL, _R(val.regs[0].id), _R(buf->regs[0].id), _R(key_reg)))
				PSS_COMP_RAISE_INT(comp, CODE);

			if(ERROR_CODE(int) == pss_comp_rmtmp(comp, key_reg))
				ERROR_RETURN_LOG(int, "Cannot release the key register");

			if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
				ERROR_RETURN_LOG(int, "Cannot release the value register");
		}
		else if(ahead[0]->type == PSS_COMP_LEX_TOKEN_RBRACE) break;
		/* TODO: handle the servlet interconnections */
		else PSS_COMP_RAISE_SYN(int, comp, "Invalid dict/service literal");

		const pss_comp_lex_token_t* tok_next = pss_comp_peek(comp, 0);
		if(NULL == tok_next) ERROR_RETURN_LOG(int, "Cannot peek the token next");
		if(tok_next->type == PSS_COMP_LEX_TOKEN_COMMA || tok_next->type == PSS_COMP_LEX_TOKEN_SEMICOLON)
		{
			if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
				ERROR_RETURN_LOG(int, "Cannot consume the token");
		}
	}

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RBRACE))
		ERROR_RETURN_LOG(int, "Right parenthesis expected");

	return 0;
}
