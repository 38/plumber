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

#define _S(what) PSS_BYTECODE_ARG_STRING(what)

#define _R(what) PSS_BYTECODE_ARG_REGISTER(what)

#define _N(what) PSS_BYTECODE_ARG_NUMERIC(what)

#define _L(what) PSS_BYTECODE_ARG_LABEL(what)

#define _INST(segment, opcode, args...) (ERROR_CODE(pss_bytecode_addr_t) != pss_bytecode_segment_append_code(segment, PSS_BYTECODE_OPCODE_##opcode, ##args, PSS_BYTECODE_ARG_END))

static inline int _expr_stmt(pss_comp_t* comp)
{
	pss_comp_value_t val = {};

	if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot parse the value");

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot release the value");

	return 0;
}

static inline int _if_stmt(pss_comp_t* comp, pss_bytecode_segment_t* seg)
{
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
	if(ERROR_CODE(int) == pss_comp_open_control_block(comp, 0))
		ERROR_RETURN_LOG(int, "Cannot open the control block");
	
	/* Open the control block for the then clause */
	if(ERROR_CODE(int) == pss_comp_open_control_block(comp, 0))
		ERROR_RETURN_LOG(int, "Cannot open the control block");

	pss_bytecode_label_t lelse = pss_comp_last_control_block_end(comp, 0);
	if(ERROR_CODE(pss_bytecode_label_t) == lelse)
		ERROR_RETURN_LOG(int, "Cannot get the end label of the control block");

	pss_bytecode_regid_t r_target;
	if(ERROR_CODE(pss_bytecode_regid_t) == (r_target = pss_comp_mktmp(comp)))
		ERROR_RETURN_LOG(int, "Cannot create target register");

	if(!_INST(seg, INT_LOAD, _L(lelse), _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, JZ, _R(val.regs[0].id), _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot release the used value");


	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_target))
		ERROR_RETURN_LOG(int, "Cannot release the target register");

	if(ERROR_CODE(int) == pss_comp_stmt_parse(comp))
		ERROR_RETURN_LOG(int, "Cannot parse the then block");
	
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek token");

	if(ahead->type == PSS_COMP_LEX_TOKEN_SEMICOLON)
	{
		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
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

		if(!_INST(seg, INT_LOAD, _L(lend), _R(r_target)))
			PSS_COMP_RAISE_INT(comp, CODE);

		if(!_INST(seg, JUMP, _R(r_target)))
			PSS_COMP_RAISE_INT(comp, CODE);

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

		if(ERROR_CODE(int) == pss_comp_stmt_parse(comp))
			ERROR_RETURN_LOG(int, "Cannot parse the then block");
	}

	if(ERROR_CODE(int) == pss_comp_close_control_block(comp))
		ERROR_RETURN_LOG(int, "Cannot close the last control block");
	
	return 0;
}

static inline int _while_stmt(pss_comp_t* comp, pss_bytecode_segment_t* seg)
{
	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_WHILE))
		ERROR_RETURN_LOG(int, "While token expected");

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LPARENTHESIS))
		ERROR_RETURN_LOG(int, "`(' expected");

	if(ERROR_CODE(int) == pss_comp_open_scope(comp))
		ERROR_RETURN_LOG(int, "Cannot open the loop scope");

	if(ERROR_CODE(int) == pss_comp_open_control_block(comp, 1))
		ERROR_RETURN_LOG(int, "Cannot open the control block");

	pss_comp_value_t val = {};
	pss_bytecode_addr_t begin;
	pss_bytecode_label_t lend;
	pss_bytecode_regid_t r_target;

	if(ERROR_CODE(pss_bytecode_addr_t) == (begin = pss_comp_last_control_block_begin(comp, 0)))
		ERROR_RETURN_LOG(int, "Cannot get the begin address of current control block");

	if(ERROR_CODE(pss_bytecode_label_t) == (lend = pss_comp_last_control_block_end(comp, 0)))
		ERROR_RETURN_LOG(int, "Cannot get the end label of the current control block");

	if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot parse the while condition expression");

	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RPARENTHESIS))
		ERROR_RETURN_LOG(int, "`)' expected");
	
	if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot simplify the value");


	if(ERROR_CODE(pss_bytecode_regid_t) == (r_target = pss_comp_mktmp(comp)))
		ERROR_RETURN_LOG(int, "Cannot allocate temp register for the jump target");

	if(!_INST(seg, INT_LOAD, _L(lend), _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, JZ, _R(val.regs[0].id), _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &val) ||
	   ERROR_CODE(int) == pss_comp_rmtmp(comp, r_target))
		ERROR_RETURN_LOG(int, "Cannot release the unused registers");

	if(ERROR_CODE(int) == pss_comp_stmt_parse(comp))
		ERROR_RETURN_LOG(int, "Cannot parse the loop body");

	if(ERROR_CODE(pss_bytecode_regid_t) == (r_target = pss_comp_mktmp(comp)))
		ERROR_RETURN_LOG(int, "Cannot allocate tmp register for the jump target");

	if(!_INST(seg, INT_LOAD, _N(begin), _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, JUMP, _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_target))
		ERROR_RETURN_LOG(int, "Cannot reliease the unseud registers");

	if(ERROR_CODE(int) == pss_comp_close_control_block(comp))
		ERROR_RETURN_LOG(int, "Cannot close the control block");
	
	if(ERROR_CODE(int) == pss_comp_close_scope(comp))
		ERROR_RETURN_LOG(int, "Cannot close the loop scope");
	return 0;
}

static inline int _break_or_continue(pss_comp_t* comp, pss_comp_lex_keyword_t tok, pss_bytecode_segment_t* seg)
{
	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, tok))
		ERROR_RETURN_LOG(int, "Unexecpted token");

	pss_bytecode_regid_t r_target;
	if(ERROR_CODE(pss_bytecode_regid_t) == (r_target = pss_comp_mktmp(comp)))
		ERROR_RETURN_LOG(int, "Cannot allocate temp register for the jump target");

	if(tok == PSS_COMP_LEX_KEYWORD_CONTINUE)
	{
		pss_bytecode_addr_t addr = pss_comp_last_loop_begin(comp);
		if(ERROR_CODE(pss_bytecode_addr_t) == addr)
			ERROR_RETURN_LOG(int, "Not inside a loop");

		if(!_INST(seg, INT_LOAD, _N(addr), _R(r_target)))
			PSS_COMP_RAISE_INT(comp, CODE);
	}
	else
	{
		pss_bytecode_label_t label = pss_comp_last_loop_end(comp);
		if(ERROR_CODE(pss_bytecode_label_t) == label)
			ERROR_RETURN_LOG(int, "Not inside a loop");

		if(!_INST(seg, INT_LOAD, _L(label), _R(r_target)))
			PSS_COMP_RAISE_INT(comp, CODE);
	}

	if(!_INST(seg, JUMP, _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_target))
		ERROR_RETURN_LOG(int, "Cannot reliease the unseud registers");

	return 0;
}

static inline int _return_stmt(pss_comp_t* comp, pss_bytecode_segment_t* seg)
{
	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_RETURN))
		ERROR_RETURN_LOG(int, "Cannot get the current key word");

	pss_comp_value_t val = {};

	if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot parse the value");

	if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot simplify the value");

	if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_RETURN, PSS_BYTECODE_ARG_REGISTER(val.regs[0].id), PSS_BYTECODE_ARG_END))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
		ERROR_RETURN_LOG(int, "Cannot release the value");
	
	return 0;
}

static inline int _foreach_stmt(pss_comp_t* comp, pss_bytecode_segment_t* seg)
{
	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek ahead token");
	
	if(ERROR_CODE(int) == pss_comp_open_scope(comp))
		ERROR_RETURN_LOG(int, "Cannot open the loop scope");

	pss_comp_value_t ctl_var = {};
	pss_comp_value_t set = {};
	
	if(ahead->type == PSS_COMP_LEX_TOKEN_KEYWORD && ahead->value.k == PSS_COMP_LEX_KEYWORD_VAR)
	{
		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			ERROR_RETURN_LOG(int, "Cannot consume ahead token");
		if(NULL == (ahead = pss_comp_peek(comp, 0)))
			ERROR_RETURN_LOG(int, "Cannot peek the next token");

		if(ahead->type != PSS_COMP_LEX_TOKEN_IDENTIFIER)
			PSS_COMP_RAISE_SYN(int, comp, "Identifier expected");

		ctl_var.kind = PSS_COMP_VALUE_KIND_REG;
		if(ERROR_CODE(pss_bytecode_regid_t) == (ctl_var.regs[0].id = pss_comp_decl_local_var(comp, ahead->value.s)))
			ERROR_RETURN_LOG(int, "Cannot declare new variable");
		ctl_var.regs[0].tmp = 0;

		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			ERROR_RETURN_LOG(int, "Cannot consume token");
	}
	else if(ERROR_CODE(int) == pss_comp_value_parse(comp, &ctl_var))
		ERROR_RETURN_LOG(int, "Cannot parse the initializer");

	if(!pss_comp_value_is_lvalue(&ctl_var))
		PSS_COMP_RAISE_SYN(int, comp, "L-Value expected");

	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_IN))
		ERROR_RETURN_LOG(int, "Keyword in expected");

	if(ERROR_CODE(int) == pss_comp_expr_parse(comp, &set))
		ERROR_RETURN_LOG(int, "Invalid expression");
	
	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_RPARENTHESIS))
		ERROR_RETURN_LOG(int, "right parenthesis expected");


	if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &set))
		ERROR_RETURN_LOG(int, "Cannot simplify the value");

	pss_bytecode_regid_t r_num = pss_comp_mktmp(comp);
	if(r_num == ERROR_CODE(pss_bytecode_regid_t))
		ERROR_RETURN_LOG(int, "Cannot allocate the temp register");

	if(!_INST(seg, LENGTH, _R(set.regs[0].id), _R(r_num)))
		PSS_COMP_RAISE_INT(comp, CODE);

	pss_bytecode_regid_t r_cnt = pss_comp_mktmp(comp);
	if(r_cnt == ERROR_CODE(pss_bytecode_regid_t))
		ERROR_RETURN_LOG(int, "Cannot allocate the counter register");

	if(!_INST(seg, INT_LOAD, _N(0), _R(r_cnt)))
		PSS_COMP_RAISE_INT(comp, CODE);

	// r_num, r_cnt, ctl_var, set

	if(ERROR_CODE(int) == pss_comp_open_control_block(comp, 1))
		ERROR_RETURN_LOG(int, "Cannot open the control block");

	pss_bytecode_addr_t begin = pss_comp_last_control_block_begin(comp, 0);
	pss_bytecode_label_t end  = pss_comp_last_control_block_end(comp, 0);

	pss_bytecode_regid_t r_cmpresult = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == r_cmpresult)
		ERROR_RETURN_LOG(int, "Cannot create the compare result regiser");
	
	pss_bytecode_regid_t r_target = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == r_target)
		ERROR_RETURN_LOG(int, "Cannot create the target register");

	if(!_INST(seg, LT, _R(r_cnt), _R(r_num), _R(r_cmpresult)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, INT_LOAD, _L(end), _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, JZ, _R(r_cmpresult), _R(r_target)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_target))
		ERROR_RETURN_LOG(int, "Cannot release the target register");

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_cmpresult))
		ERROR_RETURN_LOG(int, "Cannot release the comparasion result");

	pss_bytecode_regid_t key = pss_comp_mktmp(comp);

	if(ERROR_CODE(pss_bytecode_regid_t) == key)
		ERROR_RETURN_LOG(int, "Cannot allocate tmp register for key");

	if(!_INST(seg, GET_KEY, _R(set.regs[0].id), _R(r_cnt), _R(key)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ctl_var.kind == PSS_COMP_VALUE_KIND_REG)
	{
		if(!_INST(seg, MOVE, _R(key), _R(ctl_var.regs[0].id)))
			PSS_COMP_RAISE_INT(comp, CODE);
	}
	else if(ctl_var.kind == PSS_COMP_VALUE_KIND_DICT)
	{
		if(!_INST(seg, SET_VAL, _R(key), _R(ctl_var.regs[0].id), _R(ctl_var.regs[1].id)))
			PSS_COMP_RAISE_INT(comp, CODE);
	}
	else if(ctl_var.kind == PSS_COMP_VALUE_KIND_GLOBAL)
	{
		if(!_INST(seg, GLOBAL_SET, _R(key), _R(ctl_var.regs[0].id)))
			PSS_COMP_RAISE_INT(comp, CODE);
	}

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, key))
		ERROR_RETURN_LOG(int, "Cannot release the key register");

	if(ERROR_CODE(int) == pss_comp_stmt_parse(comp))
		ERROR_RETURN_LOG(int, "Cannot parse the loop body");

	pss_bytecode_regid_t one = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == one)
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the register");

	if(!_INST(seg, INT_LOAD, _N(1), _R(one)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, ADD, _R(one), _R(r_cnt), _R(r_cnt)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, INT_LOAD, _N(begin), _R(one)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, JUMP, _R(one)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, one))
		ERROR_RETURN_LOG(int, "Cannot release the constant register");

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_num))
		ERROR_RETURN_LOG(int, "Cannot release the number register");

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, r_cnt))
		ERROR_RETURN_LOG(int, "Cannot release the cnt register");

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &ctl_var))
		ERROR_RETURN_LOG(int, "Cannot release the control variable");

	if(ERROR_CODE(int) == pss_comp_value_release(comp, &set))
		ERROR_RETURN_LOG(int, "Cannot release the set");

	if(ERROR_CODE(int) == pss_comp_close_control_block(comp))
		ERROR_RETURN_LOG(int, "Cannot close the control block");
	
	if(ERROR_CODE(int) == pss_comp_close_scope(comp))
		ERROR_RETURN_LOG(int, "Cannot close the loop scope");

	return 0;
}

static inline int _for_stmt(pss_comp_t* comp, pss_bytecode_segment_t* seg)
{
	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_FOR))
		ERROR_RETURN_LOG(int, "Keyword for expected");
	
	if(ERROR_CODE(int) == pss_comp_expect_token(comp, PSS_COMP_LEX_TOKEN_LPARENTHESIS))
		ERROR_RETURN_LOG(int, "Keyword for expected");

	const pss_comp_lex_token_t* ahead[3] = {pss_comp_peek(comp, 0), pss_comp_peek(comp, 1), pss_comp_peek(comp, 2)};
	if(NULL == ahead[0] || NULL == ahead[1] || NULL == ahead[2]) ERROR_RETURN_LOG(int, "Cannot peek ahead token");
	
	int var_begin = 0;
	if(ahead[0]->type == PSS_COMP_LEX_TOKEN_KEYWORD && ahead[0]->value.k == PSS_COMP_LEX_KEYWORD_VAR)
		var_begin = 1;
	if(ahead[var_begin]->type == PSS_COMP_LEX_TOKEN_IDENTIFIER && 
	   ahead[var_begin + 1]->type == PSS_COMP_LEX_TOKEN_KEYWORD &&
	   ahead[var_begin + 1]->value.k == PSS_COMP_LEX_KEYWORD_IN)
		return _foreach_stmt(comp, seg);
#if 0	
	if(ERROR_CODE(int) == pss_comp_open_scope(comp))
		ERROR_RETURN_LOG(int, "Cannot open the loop scope");


	if(ahead[0]->type == PSS_COMP_LEX_TOKEN_SEMICOLON)
	{
		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			ERROR_RETURN_LOG(int, "Cannot consume the next token");
	}
	else if(ahead[0]->type == PSS_COMP_LEX_TOKEN_KEYWORD && ahead[0]->value.k == PSS_COMP_LEX_KEYWORD_VAR)
	{
		if(ERROR_CODE(int) == _var_decl_stmt(comp, seg))
			ERROR_RETURN_LOG(int, "Cannot parse the initializer");
	}
	else if(ERROR_CODE(int) == _expr_stmt(comp, seg))
		ERROR_RETURN_LOG(int, "Cannot parse the initializer");




	if(ERROR_CODE(int) == pss_comp_close_scope(comp))
		ERROR_RETURN_LOG(int, "Cannot close the loop scope");
	return 0;
#endif
	return pss_comp_raise(comp, "Fixme: for loop is not supported");
}

static inline int _var_decl_stmt(pss_comp_t* comp, pss_bytecode_segment_t* seg)
{
	if(ERROR_CODE(int) == pss_comp_expect_keyword(comp, PSS_COMP_LEX_KEYWORD_VAR))
		ERROR_RETURN_LOG(int, "Keyword var expecteed");

	for(;;)
	{
		const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
		if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek the token ahead");

		if(ahead->type != PSS_COMP_LEX_TOKEN_IDENTIFIER)
			PSS_COMP_RAISE_SYN(int, comp, "Identifer expected");

		pss_bytecode_regid_t reg = pss_comp_decl_local_var(comp, ahead->value.s);
		if(ERROR_CODE(pss_bytecode_regid_t) == reg) 
			ERROR_RETURN_LOG(int, "Cannot put the local variable to the environment");

		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			ERROR_RETURN_LOG(int, "Cannot consume the next token");

		if(NULL == (ahead = pss_comp_peek(comp, 0)))
			ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

		if(ahead->type == PSS_COMP_LEX_TOKEN_EQUAL)
		{
			if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
				ERROR_RETURN_LOG(int, "Cannot consume the next token");

			/* This means it have a initial value clause */
			pss_comp_value_t val = {};
			if(ERROR_CODE(int) == pss_comp_value_parse(comp, &val))
				ERROR_RETURN_LOG(int, "Cannot parse the initializer");

			if(ERROR_CODE(int) == pss_comp_value_simplify(comp, &val))
				ERROR_RETURN_LOG(int, "Cannot simplify the value");

			if(!_INST(seg, MOVE, _R(val.regs[0].id), _R(reg)))
				PSS_COMP_RAISE_INT(comp, CODE);

			if(ERROR_CODE(int) == pss_comp_value_release(comp, &val))
				ERROR_RETURN_LOG(int, "Cannot release value");

			if(NULL == (ahead = pss_comp_peek(comp, 0)))
				ERROR_RETURN_LOG(int, "Cannot peek the ahead token");
		}

		if(ahead->type != PSS_COMP_LEX_TOKEN_COMMA) break;

		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
			ERROR_RETURN_LOG(int, "Cannot consume the comma token");
	}

	return 0;
}

int pss_comp_stmt_parse(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get current bytecode segment");

	const pss_comp_lex_token_t* ahead = pss_comp_peek(comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek the token ahead");

	int rc = 0;
	switch(ahead->type)
	{
		case PSS_COMP_LEX_TOKEN_SEMICOLON:
			if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
				ERROR_RETURN_LOG(int, "Cannot consume the ahead token");
			break;
		case PSS_COMP_LEX_TOKEN_KEYWORD:
			switch(ahead->value.k)
			{
				case PSS_COMP_LEX_KEYWORD_RETURN:
					rc = _return_stmt(comp, seg);
					break;
				case PSS_COMP_LEX_KEYWORD_IF:
					rc = _if_stmt(comp, seg);
					break;
				case PSS_COMP_LEX_KEYWORD_WHILE:
					rc = _while_stmt(comp, seg);
					break;
				case PSS_COMP_LEX_KEYWORD_CONTINUE:
				case PSS_COMP_LEX_KEYWORD_BREAK:
					rc = _break_or_continue(comp, ahead->value.k, seg);
					break;
				case PSS_COMP_LEX_KEYWORD_VAR:
					rc = _var_decl_stmt(comp, seg);
					break;
				case PSS_COMP_LEX_KEYWORD_FOR:
					rc = _for_stmt(comp, seg);
					break;
				default:
					PSS_COMP_RAISE_SYN(int, comp, "Unexpected keyword");
			}
			break;
		case PSS_COMP_LEX_TOKEN_LBRACE:
			if(ERROR_CODE(int) == pss_comp_block_parse(comp, PSS_COMP_LEX_TOKEN_LBRACE, PSS_COMP_LEX_TOKEN_RBRACE))
				ERROR_RETURN_LOG(int, "Invalid block statement");
			break;
		default:
			if(ERROR_CODE(int) == _expr_stmt(comp))
				ERROR_RETURN_LOG(int, "Invalid expression statement");
	}

	if(ERROR_CODE(int) == rc) 
		ERROR_RETURN_LOG(int, "Cannot parse the code block");

	return rc;
}
