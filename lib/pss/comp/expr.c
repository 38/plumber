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

#define _S(what) PSS_BYTECODE_ARG_STRING(what)

#define _R(what) PSS_BYTECODE_ARG_REGISTER(what)

#define _N(what) PSS_BYTECODE_ARG_NUMERIC(what)

#define _L(what) PSS_BYTECODE_ARG_LABEL(what)

#define _INST(segment, opcode, args...) (ERROR_CODE(pss_bytecode_addr_t) != pss_bytecode_segment_append_code(segment, PSS_BYTECODE_OPCODE_##opcode, ##args, PSS_BYTECODE_ARG_END))

/**
 * @brief The accosiativity of the operator, 0 means left accosiative, 1 means right accosiative */
static const int _associativity[PSS_COMP_LEX_TOKEN_NUM_OF_ENTRIES] = {
	[PSS_COMP_LEX_TOKEN_EQUAL]           = 1,
	[PSS_COMP_LEX_TOKEN_ADD_EQUAL]       = 1,
	[PSS_COMP_LEX_TOKEN_MINUS_EQUAL]     = 1,
	[PSS_COMP_LEX_TOKEN_TIMES_EQUAL]     = 1,
	[PSS_COMP_LEX_TOKEN_DIVIDE_EQUAL]    = 1,
	[PSS_COMP_LEX_TOKEN_MODULAR_EQUAL]   = 1
};

/**
 * @brief The priority for each operator, 0 indicates this is not a valid operator
 **/
static const int _priority[PSS_COMP_LEX_TOKEN_NUM_OF_ENTRIES] = {
	[PSS_COMP_LEX_TOKEN_EQUAL]         = 1,
	[PSS_COMP_LEX_TOKEN_ADD_EQUAL]     = 1,
	[PSS_COMP_LEX_TOKEN_MINUS_EQUAL]   = 1,
	[PSS_COMP_LEX_TOKEN_TIMES_EQUAL]   = 1,
	[PSS_COMP_LEX_TOKEN_DIVIDE_EQUAL]  = 1,
	[PSS_COMP_LEX_TOKEN_MODULAR_EQUAL] = 1,
	[PSS_COMP_LEX_TOKEN_AND]           = 2,
	[PSS_COMP_LEX_TOKEN_OR]            = 2,
	[PSS_COMP_LEX_TOKEN_EQUALEQUAL]    = 3,
	[PSS_COMP_LEX_TOKEN_NE]            = 3,
	[PSS_COMP_LEX_TOKEN_LT]            = 3,
	[PSS_COMP_LEX_TOKEN_LE]            = 3,
	[PSS_COMP_LEX_TOKEN_GT]            = 3,
	[PSS_COMP_LEX_TOKEN_GE]            = 3,
	[PSS_COMP_LEX_TOKEN_ADD]           = 4,
	[PSS_COMP_LEX_TOKEN_MINUS]         = 4,
	[PSS_COMP_LEX_TOKEN_DIVIDE]        = 5,
	[PSS_COMP_LEX_TOKEN_TIMES]         = 5,
	[PSS_COMP_LEX_TOKEN_MODULAR]       = 5
};

#define _TOMAP(from, to) [PSS_COMP_LEX_TOKEN_##from] = PSS_BYTECODE_OPCODE_##to
/**
 * @brief The opcode mapped to the operator for the binary
 * @note We actually handle the assignment differently
 **/
static const pss_bytecode_opcode_t _opcode[PSS_COMP_LEX_TOKEN_NUM_OF_ENTRIES] = {
	_TOMAP(AND, AND),
	_TOMAP(OR, OR),
	_TOMAP(EQUALEQUAL, EQ),
	_TOMAP(LT, LT),
	_TOMAP(LE, LE),
	_TOMAP(GT, GT),
	_TOMAP(GE, GE),
	_TOMAP(NE, NE),
	_TOMAP(ADD, ADD),
	_TOMAP(MINUS, SUB),
	_TOMAP(TIMES, MUL),
	_TOMAP(DIVIDE, DIV),
	_TOMAP(MODULAR, MOD),
	_TOMAP(ADD_EQUAL, ADD),
	_TOMAP(MINUS_EQUAL, SUB),
	_TOMAP(TIMES_EQUAL, MUL),
	_TOMAP(DIVIDE_EQUAL, DIV),
	_TOMAP(MODULAR_EQUAL, MOD)
};

int pss_comp_expr_parse(pss_comp_t* comp, pss_comp_value_t* buf)
{
	if(NULL == comp || NULL == buf) PSS_COMP_RAISE_INT(comp, ARGS);

	pss_bytecode_segment_t* seg;
	const pss_comp_lex_token_t* ahead;

	if(NULL == (seg = pss_comp_get_code_segment(comp))) PSS_COMP_RAISE_INT(comp, SEG);

	pss_comp_value_t          vs[128];
	pss_comp_lex_token_type_t ts[128];
	uint32_t sp = 0;

	for(;;)
	{
		if(ERROR_CODE(int) == pss_comp_value_parse(comp, vs + sp))
		    ERROR_RETURN_LOG(int, "Cannot prase next valid value");

		if(NULL == (ahead = pss_comp_peek(comp, 0)))
		    ERROR_RETURN_LOG(int, "Cannot peek ahead token");

		int p = _priority[ahead->type];

		for(;sp > 0 && (_priority[ts[sp - 1]] > p || (_priority[ts[sp - 1]] == p && _associativity[ts[sp - 1]] == 0)); sp --)
		{
			if(ts[sp - 1] == PSS_COMP_LEX_TOKEN_ADD_EQUAL ||
			   ts[sp - 1] == PSS_COMP_LEX_TOKEN_MINUS_EQUAL ||
			   ts[sp - 1] == PSS_COMP_LEX_TOKEN_TIMES_EQUAL ||
			   ts[sp - 1] == PSS_COMP_LEX_TOKEN_DIVIDE_EQUAL ||
			   ts[sp - 1] == PSS_COMP_LEX_TOKEN_MODULAR_EQUAL)
			{
				pss_bytecode_regid_t lval = pss_comp_mktmp(comp);
				if(ERROR_CODE(pss_bytecode_regid_t) == lval)
				    ERROR_RETURN_LOG(int, "Cannot make the temp register for the L-Value");

				switch(vs[sp - 1].kind)
				{
					case PSS_COMP_VALUE_KIND_REG:
					    if(!_INST(seg, MOVE, _R(vs[sp - 1].regs[0].id), _R(lval)))
					        PSS_COMP_RAISE_INT(comp, CODE);
					    break;
					case PSS_COMP_VALUE_KIND_DICT:
					    if(!_INST(seg, GET_VAL, _R(vs[sp - 1].regs[0].id), _R(vs[sp - 1].regs[1].id), _R(lval)))
					        PSS_COMP_RAISE_INT(comp, CODE);
					    break;
					case PSS_COMP_VALUE_KIND_GLOBAL:
					    if(!_INST(seg, GLOBAL_GET, _R(vs[sp - 1].regs[0].id), _R(lval)))
					        PSS_COMP_RAISE_INT(comp, CODE);
					    break;
					case PSS_COMP_VALUE_KIND_GLOBAL_DICT:
					    PSS_COMP_RAISE_SYN(int, comp, "Malformed global accessor");
				}

				if(ERROR_CODE(int) == pss_comp_value_simplify(comp, vs + sp))
				    ERROR_RETURN_LOG(int, "Cannot simplify the right operand");

				pss_bytecode_opcode_t opcode = _opcode[ts[sp - 1]];

				if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, opcode,
				            PSS_BYTECODE_ARG_REGISTER(lval),
				            PSS_BYTECODE_ARG_REGISTER(vs[sp].regs[0].id),
				            PSS_BYTECODE_ARG_REGISTER(lval),
				            PSS_BYTECODE_ARG_END))
				    PSS_COMP_RAISE_INT(comp, CODE);

				if(ERROR_CODE(int) == pss_comp_value_release(comp, vs + sp))
					ERROR_RETURN_LOG(int, "Cannot release the R-Value");

				vs[sp].regs[0].id = lval;
				vs[sp].regs[0].tmp = 1;
				vs[sp].kind = PSS_COMP_VALUE_KIND_REG;
				ts[sp - 1] = PSS_COMP_LEX_TOKEN_EQUAL;
			}
			if(ts[sp - 1] == PSS_COMP_LEX_TOKEN_EQUAL)
			{
				if(!pss_comp_value_is_lvalue(vs + sp - 1))
				    PSS_COMP_RAISE_SYN(int, comp, "Got R-Value on the left side of assignment operator");
				if(ERROR_CODE(int) == pss_comp_value_simplify(comp, vs + sp))
				    ERROR_RETURN_LOG(int, "Cannot simplify the right operand");
				switch(vs[sp - 1].kind)
				{
					case PSS_COMP_VALUE_KIND_REG:
					    if(!_INST(seg, MOVE, _R(vs[sp].regs[0].id), _R(vs[sp - 1].regs[0].id)))
					        PSS_COMP_RAISE_INT(comp, CODE);
					    break;
					case PSS_COMP_VALUE_KIND_DICT:
					    if(!_INST(seg, SET_VAL, _R(vs[sp].regs[0].id), _R(vs[sp - 1].regs[0].id), _R(vs[sp - 1].regs[1].id)))
					        PSS_COMP_RAISE_INT(comp, CODE);
					    break;
					case PSS_COMP_VALUE_KIND_GLOBAL:
					    if(!_INST(seg, GLOBAL_SET, _R(vs[sp].regs[0].id), _R(vs[sp - 1].regs[0].id)))
					        PSS_COMP_RAISE_INT(comp, CODE);
					    break;
					case PSS_COMP_VALUE_KIND_GLOBAL_DICT:
					    PSS_COMP_RAISE_SYN(int, comp, "Malformed global accessor");
				}

				if(ERROR_CODE(int) == pss_comp_value_release(comp, vs + sp - 1))
				    ERROR_RETURN_LOG(int, "Cannot release the L-Value");
				vs[sp - 1] = vs[sp];
			}
			else
			{
				if(_priority[ts[sp - 1]] == 0)
				    PSS_COMP_RAISE_SYN(int, comp, "Invalid operator");

				pss_bytecode_opcode_t opcode = _opcode[ts[sp - 1]];

				if(ERROR_CODE(int) == pss_comp_value_simplify(comp, vs + sp))
				    ERROR_RETURN_LOG(int, "Cannot simplify the right operand");

				if(ERROR_CODE(int) == pss_comp_value_simplify(comp, vs + sp - 1))
				    ERROR_RETURN_LOG(int, "Cannot simplify the left operand");

				pss_comp_value_t result = { .kind = PSS_COMP_VALUE_KIND_REG };

				if(ERROR_CODE(pss_bytecode_regid_t) == (result.regs[0].id = pss_comp_mktmp(comp)))
				    ERROR_RETURN_LOG(int, "Cannot create new result register");
				result.regs[0].tmp = 1;

				if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, opcode,
				            PSS_BYTECODE_ARG_REGISTER(vs[sp - 1].regs[0].id),
				            PSS_BYTECODE_ARG_REGISTER(vs[sp].regs[0].id),
				            PSS_BYTECODE_ARG_REGISTER(result.regs[0].id),
				            PSS_BYTECODE_ARG_END))
				    PSS_COMP_RAISE_INT(comp, CODE);

				if(ERROR_CODE(int) == pss_comp_value_release(comp, vs + sp))
				    ERROR_RETURN_LOG(int, "Cannot release the used value");

				if(ERROR_CODE(int) == pss_comp_value_release(comp, vs + sp - 1))
				    ERROR_RETURN_LOG(int, "Cannot release the used value");

				vs[sp-1] = result;
			}
		}

		if(_priority[ahead->type] == 0) break;
		ts[sp ++] = ahead->type;
		if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
		    ERROR_RETURN_LOG(int, "Cannot consume ahead token");
	}

	*buf = vs[0];
	return 0;
}
