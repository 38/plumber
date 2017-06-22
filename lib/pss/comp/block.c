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

int pss_comp_block_parse(pss_comp_t* comp, pss_comp_lex_token_type_t first_token, pss_comp_lex_token_type_t last_token)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	if(first_token != PSS_COMP_LEX_TOKEN_NAT && ERROR_CODE(int) == pss_comp_expect_token(comp, first_token))
		ERROR_RETURN_LOG(int, "Unexpected beginging of the header");

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
		
		if(ERROR_CODE(int) == pss_comp_stmt_parse(comp))
			ERROR_RETURN_LOG(int, "Cannot parse the next statement");

	}

	return 0;
}
