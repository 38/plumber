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

int pss_comp_stmt_expr(pss_comp_t* comp, pss_comp_stmt_result_t* result)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Invalid arguments");

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

int pss_comp_stmt_return(pss_comp_t* comp, pss_comp_stmt_result_t* result)
{
	if(NULL == comp) PSS_COMP_RAISE_RETURN(int, comp, "Internal error: Invalid arguments");

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

