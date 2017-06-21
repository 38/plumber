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
#include <pss/comp/value.h>
#include <pss/comp/expr.h>
#include <pss/comp/stmt.h>

static inline int _expr_stmt(pss_comp_t* comp, pss_comp_stmt_result_t* result)
{
	pss_bytecode_segment_t* seg;
	if(NULL == (seg = pss_comp_get_code_segment(comp)))
		ERROR_RETURN_LOG(int, "Cannot get current code segment");

	if(NULL != result)
		result->begin = result->end = pss_bytecode_segment_length(seg);

	pss_comp_value_t val = {};

	if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot parse the value");

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot release the value");

	if(NULL != result) result->end = pss_bytecode_segment_length(seg);

	return 0;
}

static inline int _if_stmt(pss_comp_t* comp, pss_comp_stmt_result_t* result)
{
	pss_bytecode_segment_t* seg;
	if(NULL == (seg = pss_comp_get_code_segment(comp)))
		ERROR_RETURN_LOG(int, "Cannot get current code segment");

	if(NULL != result)
		result->begin = result->end = pss_bytecode_segment_length(seg);
	
	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_IF))
		ERROR_RETURN_LOG(int, "Invalid if statement");

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LPARENTHESIS))
		ERROR_RETURN_LOG(int, "Invalid condition variable");

	pss_comp_value_t val = {};

	if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot parse expression");

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RPARENTHESIS))
		ERROR_RETURN_LOG(int, "Invalid condition expression");

	if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot simplify the value");

	/* Open the block for the entire if...else... */
	if(ERROR_CODE(int) == pss_comp_open_control_block(comp))
		ERROR_RETURN_LOG(int, "Cannot open the control block");
	
	/* Open the control block for the then clause */
	if(ERROR_CODE(int) == pss_comp_open_control_block(comp))
		ERROR_RETURN_LOG(int, "Cannot open the control block");

	pss_bytecode_label_t lelse = pss_comp_last_control_block_end(comp, 0);
	if(ERROR_CODE(pss_bytecode_label_t) == lelse)
		ERROR_RETURN_LOG(int, "Cannot get the end label of the control block");

	pss_bytecode_regid_t r_target;
	if(ERROR_CODE(pss_bytecode_regid_t) == (r_target = pss_comp_mktmp(comp)))
		ERROR_RETURN_LOG(int, "Cannot create target register");

	if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_INT_LOAD, 
				                                                                PSS_BYTECODE_ARG_LABEL(lelse), 
																				PSS_BYTECODE_ARG_REGISTER(r_target),
																				PSS_BYTECODE_ARG_END))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Cannot append instruction");
	if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_JZ,
																				PSS_BYTECODE_ARG_REGISTER(val.regs[0].id),
																				PSS_BYTECODE_ARG_REGISTER(r_target),
																				PSS_BYTECODE_ARG_END))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Cannot append instruction");

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot release the used value");


	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_target))
		ERROR_RETURN_LOG(int, "Cannot release the target register");

	pss_comp_stmt_result_t then_block;
	if(ERROR_CODE(int) == pss_comp_stmt_parse(comp, &then_block))
		ERROR_RETURN_LOG(int, "Cannot parse the then block");
	
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek token");

	if(ahead->type == PSS_COMP_LEX_TOKEN_SEMICOLON)
	{
		if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
			ERROR_RETURN_LOG(int, "Cannot consume the token");
		if(NULL == (ahead = pss_comp_peek(comp, 0)))
			ERROR_RETURN_LOG(int, "Cannot peek ahead token");
	}

	if(ahead->type == PSS_COMP_LEX_TOKEN_KEYWORD && ahead->value.k == PSS_COMP_LEX_KEYWORD_ELSE)
	{
		pss_bytecode_label_t lend = pss_comp_last_control_block_end(comp, 1);
		if(ERROR_CODE(pss_bytecode_label_t) == lend)
			ERROR_RETURN_LOG(int, "Cannot get the end label for the control block");
		
		pss_bytecode_regid_t r_target;
		if(ERROR_CODE(pss_bytecode_regid_t) == (r_target = pss_comp_mktmp(comp)))
			ERROR_RETURN_LOG(int, "Cannot create target register");

		if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_INT_LOAD,
						                                                                PSS_BYTECODE_ARG_LABEL(lend),
																						PSS_BYTECODE_ARG_REGISTER(r_target),
																						PSS_BYTECODE_ARG_END))
			PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Cannot append instruction");

		if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_JUMP,
					                                                                PSS_BYTECODE_ARG_REGISTER(r_target),
																					PSS_BYTECODE_ARG_END))
			PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Cannot append instruction");
		if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_target))
			ERROR_RETURN_LOG(int, "Cannot release the target register");

	}

	if(ERROR_CODE(int) == pss_comp_close_control_block(comp))
		ERROR_RETURN_LOG(int, "Cannot close the last control block");

	/* Generate code for else */
	if(ahead->type == PSS_COMP_LEX_TOKEN_KEYWORD && ahead->value.k == PSS_COMP_LEX_KEYWORD_ELSE)
	{
		if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_ELSE))
			ERROR_RETURN_LOG(int, "Unexpected token");

		pss_comp_stmt_result_t else_block;
		if(ERROR_CODE(int) == pss_comp_stmt_parse(comp, &else_block))
			ERROR_RETURN_LOG(int, "Cannot parse the then block");
	}

	if(ERROR_CODE(int) == pss_comp_close_control_block(comp))
		ERROR_RETURN_LOG(int, "Cannot close the last control block");

	return 0;
}

static inline int _return_stmt(pss_comp_t* comp, pss_comp_stmt_result_t* result)
{
	pss_bytecode_segment_t* seg;
	if(NULL == (seg = pss_comp_get_code_segment(comp)))
		ERROR_RETURN_LOG(int, "Cannot get current code segment");

	if(NULL != result)
		result->begin = result->end = pss_bytecode_segment_length(seg);

	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_RETURN))
		ERROR_RETURN_LOG(int, "Cannot get the current key word");

	pss_comp_value_t val = {};

	if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot parse the value");

	if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot simplify the value");

	if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_RETURN, PSS_BYTECODE_ARG_REGISTER(val.regs[0].id), PSS_BYTECODE_ARG_END))
		PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Cannot append code");

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot release the value");
	
	if(NULL != result) result->end = pss_bytecode_segment_length(seg);
	return 0;
}


int pss_comp_stmt_parse(pss_comp_t* comp, pss_comp_stmt_result_t* result)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Invalid arguments");

	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead)
		ERROR_RETURN_LOG(int, "Cannot peek the token ahead");

	int rc = 0;
	switch(ahead->type)
	{
		case PSS_COMP_LEX_TOKEN_SEMICOLON:
			if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
				ERROR_RETURN_LOG(int, "Cannot consume the ahead token");
			break;
		case PSS_COMP_LEX_TOKEN_KEYWORD:
			switch(ahead->value.k)
			{
				case PSS_COMP_LEX_KEYWORD_RETURN:
					rc = _return_stmt(comp, result);
					break;
				case PSS_COMP_LEX_KEYWORD_IF:
					rc = _if_stmt(comp, result);
					break;
				default:
					PSS_COMP_RAISE_RETURN(int, comp, "Syntax error: Unexpected keyword");
			}
			break;
		default:
			if(ERROR_CODE(int) == _expr_stmt(comp, result))
				ERROR_RETURN_LOG(int, "Invalid expression statement");
	}

	if(ERROR_CODE(int) == rc) 
		ERROR_RETURN_LOG(int, "Cannot parse the code block");

	return rc;
}
