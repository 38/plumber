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

int pss_comp_block_parse(pss_comp_t* comp, pss_comp_lex_token_type_t first_token, pss_comp_lex_token_type_t last_token, pss_comp_block_t* result)
{
	if(NULL == comp)
		PSS_COMP_RAISE_RETURN(int, comp, "Internal Error: Invalid arguments");

	if(first_token != PSS_COMP_LEX_TOKEN_NAT)
	{
		const pss_comp_lex_token_t* actual_first = pss_comp_peek(comp, 0);
		if(NULL == actual_first) return ERROR_CODE(int);

		if(actual_first->type != first_token)
			PSS_COMP_RAISE_RETURN(int, comp, "Syntax Error: Invalid block header");

		if(ERROR_CODE(int) == pss_comp_comsume(comp, 1))
			return ERROR_CODE(int);
	}

	if(NULL != result) result->begin = result->end = 0;

	for(;;)
	{
		const pss_comp_lex_token_t* token = pss_comp_peek(comp, 0);

		if(token->type == last_token) break;

		int rc = 0;
		pss_comp_stmt_result_t stmt_result;

		switch(token->type)
		{
			case PSS_COMP_LEX_TOKEN_IDENTIFIER:
				rc = pss_comp_stmt_assignment(comp, 0,  &stmt_result);
				break;
			case PSS_COMP_LEX_TOKEN_SEMICOLON:
				continue;
			default:
				PSS_COMP_RAISE_RETURN(int, comp, "Syntax Error: Unexpected token");
		}

		if(ERROR_CODE(int) == rc) 
			ERROR_RETURN_LOG(int, "Cannot parse the code block");
	}
	return 0;
}
