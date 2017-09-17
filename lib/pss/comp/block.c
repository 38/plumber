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
#include <pss/comp/stmt.h>

#define _S(what) PSS_BYTECODE_ARG_STRING(what)

#define _R(what) PSS_BYTECODE_ARG_REGISTER(what)

#define _N(what) PSS_BYTECODE_ARG_NUMERIC(what)

#define _L(what) PSS_BYTECODE_ARG_LABEL(what)

#define _INST(segment, opcode, args...) (ERROR_CODE(pss_bytecode_addr_t) != pss_bytecode_segment_append_code(segment, PSS_BYTECODE_OPCODE_##opcode, ##args, PSS_BYTECODE_ARG_END))

int pss_comp_block_parse(pss_comp_t* comp, pss_comp_lex_token_type_t first_token, pss_comp_lex_token_type_t last_token, uint32_t repl_mode)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	if(first_token != PSS_COMP_LEX_TOKEN_NAT && ERROR_CODE(int) == pss_comp_expect_token(comp, first_token))
	    ERROR_RETURN_LOG(int, "Unexpected beginging of the header");

	if(ERROR_CODE(int) == pss_comp_open_scope(comp))
	    ERROR_RETURN_LOG(int, "Cannot open scope");

	uint32_t last_stmt_line = ERROR_CODE(uint32_t);

	pss_comp_value_t result = {.kind = PSS_COMP_VALUE_KIND_INVALID};

	for(;;)
	{
		const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
		if(NULL == ahead)
		    ERROR_RETURN_LOG(int, "Cannot peek ahead token");

		if(ahead->type == last_token)
		{
			if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			    ERROR_RETURN_LOG(int, "Cannot consume token");
			break;
		}

		int update_last_line = 0;

		if(ahead->type == PSS_COMP_LEX_TOKEN_SEMICOLON ||
		   ahead->type == PSS_COMP_LEX_TOKEN_LBRACE)
		    last_stmt_line = ERROR_CODE(uint32_t);
		else
		{
			if(ahead->line == last_stmt_line)
			    PSS_COMP_RAISE_SYN(int, comp, "';' expected");
			update_last_line = 1;
		}

		if(result.kind != PSS_COMP_VALUE_KIND_INVALID && ERROR_CODE(int) == pss_comp_value_release(comp, &result))
		    ERROR_RETURN_LOG(int, "Cannot release the expression result");

		if(ERROR_CODE(int) == pss_comp_stmt_parse(comp, repl_mode?&result:NULL))
		    ERROR_RETURN_LOG(int, "Cannot parse the next statement");

		if(update_last_line && ERROR_CODE(uint32_t) == (last_stmt_line = pss_comp_last_consumed_line(comp)))
		    ERROR_RETURN_LOG(int, "Cannot get the line number of the last consumed line");

	}

	if(repl_mode && result.kind != PSS_COMP_VALUE_KIND_INVALID)
	{
		pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
		if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get current bytecode segment");

		if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &result))
		    ERROR_RETURN_LOG(int, "Cannot simplify the result value");

		if(!_INST(seg, RETURN, _R(result.regs[0].id)))
		    PSS_COMP_RAISE_INT(comp, CODE);

		if(ERROR_CODE(int) == pss_comp_value_release(comp, &result))
		    ERROR_RETURN_LOG(int, "Cannot release the value");
	}

	if(ERROR_CODE(int) == pss_comp_close_scope(comp))
	    ERROR_RETURN_LOG(int, "Cannot close scope");

	return 0;
}
