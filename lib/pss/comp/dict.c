/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

#include <error.h>
#include <package_config.h>
#include <utils/hash/murmurhash3.h>

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

/**
 * @brief The servlet name set
 **/
typedef struct _servlet_t {
	uint64_t              hash[2];/*!< The 128 bit hash code */
	char*                 name;   /*!< The servlet name */
	pss_bytecode_regid_t  reg;    /*!< The register used for the edge set */
	struct _servlet_t*    next;   /*!< The next servlet in the servlet set */
} _servlet_t;

/**
 * @brief The compile time service context
 **/
typedef struct {
	pss_comp_t*             comp;                          /*!< The current compiler instance */
	pss_bytecode_segment_t* seg;                    /*!< The bytecode segment we are using */
	pss_bytecode_regid_t    dict;                   /*!< The register that holds the result dictionary */
	_servlet_t*             nodes[PSS_COMP_MAX_SERVLET];   /*!< The servlet hash table, which maps the servlet name to the edge adj list */
} _service_ctx_t;

uint32_t _hashcode(const char* str, size_t len, uint64_t full[2])
{
	murmurhash3_128(str, len, 0x1234567u, full);

	const uint32_t k = ((uint32_t)(0x8000000000000000ull%PSS_COMP_MAX_SERVLET) * 2) % PSS_COMP_MAX_SERVLET;

	return (uint32_t)((full[0] % PSS_COMP_MAX_SERVLET + (full[1] % PSS_COMP_MAX_SERVLET) * k) % PSS_COMP_MAX_SERVLET);
}

pss_bytecode_regid_t _get_adj_reg(_service_ctx_t* ctx, const char* name)
{
	uint64_t hash[2];
	size_t len;
	uint32_t slot = _hashcode(name, len = strlen(name), hash);
	_servlet_t* ptr;
	for(ptr = ctx->nodes[slot]; NULL != ptr && (ptr->hash[0] != hash[0] || ptr->hash[1] != hash[1]) && strcmp(ptr->name, name) != 0; ptr = ptr->next);

	if(NULL != ptr) return ptr->reg;

	_servlet_t* node = (_servlet_t*)calloc(1, sizeof(*node));
	if(NULL == node)
		return (pss_bytecode_regid_t)pss_comp_raise(ctx->comp, "Cannot allocate memory for the adj list for the servlet node");

	if(ERROR_CODE(pss_bytecode_regid_t) == (node->reg = pss_comp_mktmp(ctx->comp)))
		ERROR_LOG_ERRNO_GOTO(CREATE_ERR, "Cannot allocate register for the adj list");

	if(!_INST(ctx->seg, DICT_NEW, _R(node->reg)))
	{
		pss_comp_raise_internal(ctx->comp, PSS_COMP_INTERNAL_CODE);
		goto CREATE_ERR;
	}

	node->hash[0] = hash[0];
	node->hash[1] = hash[1];

	if(ERROR_CODE(pss_bytecode_regid_t) == (node->reg = pss_comp_mktmp(ctx->comp)))
		ERROR_LOG_ERRNO_GOTO(CREATE_ERR, "Cannot allocate new registr");

	if(NULL == (node->name = (char*)malloc(len + 1)))
		ERROR_LOG_ERRNO_GOTO(CREATE_ERR, "Cannot allocate memory for the node name");

	memcpy(node->name, name, len + 1);

	node->next = ctx->nodes[slot];
	ctx->nodes[slot] = node;
	return node->reg;
CREATE_ERR:
	if(NULL != node) free(node);
	return ERROR_CODE(pss_bytecode_regid_t);
}

_service_ctx_t* _service_ctx_new(pss_comp_t* comp, pss_bytecode_segment_t* seg, pss_bytecode_regid_t dict)
{
	_service_ctx_t* ret = (_service_ctx_t*)calloc(1, sizeof(*ret));
	if(NULL == ret)
	{
		pss_comp_raise_internal(comp, PSS_COMP_INTERNAL_MALLOC);
		return NULL;
	}

	ret->comp = comp;
	ret->seg = seg;
	ret->dict = dict;

	return ret;
}

int _service_ctx_free(_service_ctx_t* ctx)
{
	int rc = 0;
	uint32_t i;
	for(i = 0; i < sizeof(ctx->nodes) / sizeof(ctx->nodes[0]); i ++)
	{
		_servlet_t* ptr;
		for(ptr = ctx->nodes[i]; ptr != NULL; )
		{
			_servlet_t* this = ptr;
			ptr = ptr->next;

			if(NULL != this->name) free(this->name);

			char buf[1024];
			snprintf(buf, sizeof(buf), "@%s", this->name);
			pss_bytecode_regid_t regid = pss_comp_mktmp(ctx->comp);
			if(ERROR_CODE(pss_bytecode_regid_t) == regid)
				rc = ERROR_CODE(int);
			else do {
				if(!_INST(ctx->seg, STR_LOAD, _S(buf), _R(regid)))
					goto ERR;
				if(!_INST(ctx->seg, SET_VAL, _R(regid), _R(ctx->dict)))
					goto ERR;
				if(ERROR_CODE(int) == pss_comp_rmtmp(ctx->comp, regid))
					goto ERR;
				break;
ERR:
				pss_comp_raise_internal(ctx->comp, PSS_COMP_INTERNAL_CODE);
				rc = ERROR_CODE(int);
			} while(0);
			
			if(ERROR_CODE(pss_bytecode_regid_t) != this->reg && ERROR_CODE(int) == pss_comp_rmtmp(ctx->comp, this->reg))
				rc = ERROR_CODE(int);
			free(this);
		}
	}

	free(ctx);

	return rc;
}

static inline int _add_edge(_service_ctx_t* ctx, const char* left_node, const char* left_port, const char* right_port, const char* right_node)
{
	pss_comp_t* comp = ctx->comp;
	pss_bytecode_segment_t* seg = ctx->seg;
	
	if(NULL == left_node || NULL == right_node || NULL == left_port || NULL == right_port)
		PSS_COMP_RAISE_INT(comp, ARGS);

	pss_bytecode_regid_t list_reg = _get_adj_reg(ctx, left_node);
	if(ERROR_CODE(pss_bytecode_regid_t) == list_reg)
		ERROR_RETURN_LOG(int, "Cannot get the register for the adj list");

	char keybuf[1024];
	snprintf(keybuf, sizeof(keybuf), "@%s", left_port);
	char valbuf[1024];
	snprintf(valbuf, sizeof(valbuf), "%s@%s", right_node, right_port);

	pss_bytecode_regid_t keyreg = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == keyreg) ERROR_RETURN_LOG(int, "Cannot allocate register for the key");
	pss_bytecode_regid_t valreg = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == valreg) ERROR_RETURN_LOG(int, "Cannot allocate register for the val");

	if(!_INST(seg, STR_LOAD, _S(keybuf), _R(keyreg)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, STR_LOAD, _S(valbuf), _R(valreg)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, SET_VAL, _R(valreg), _R(list_reg), _R(valreg)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, keyreg))
		ERROR_RETURN_LOG(int, "Cannot release the key register");

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, valreg))
		ERROR_RETURN_LOG(int, "Cannot release the val register");

	return 0;
}

static inline int _add_port(_service_ctx_t* ctx, const char* node, const char* port, const char* name, int input)
{
	if(NULL == node || NULL == port)
		PSS_COMP_RAISE_INT(ctx->comp, ARGS);
	
	pss_comp_t* comp = ctx->comp;
	pss_bytecode_segment_t* seg = ctx->seg;

	char keybuf[1024];
	snprintf(keybuf, sizeof(keybuf), "@%s@%s", ((input)?"input":"output"), ((name)?name:""));
	char valbuf[1024];
	snprintf(keybuf, sizeof(keybuf), "%s@%s", node, port);

	pss_bytecode_regid_t keyreg = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == keyreg)
		ERROR_RETURN_LOG(int, "Cannot allocate key register");
	pss_bytecode_regid_t valreg = pss_comp_mktmp(comp);
	if(ERROR_CODE(pss_bytecode_regid_t) == valreg)
		ERROR_RETURN_LOG(int, "Cannot allocate val register");

	if(!_INST(seg, STR_LOAD, _S(keybuf), _R(keyreg)))
		PSS_COMP_RAISE_INT(comp, CODE);
	if(!_INST(seg, STR_LOAD, _S(valbuf), _R(valreg)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(!_INST(seg, SET_VAL, _R(valreg), _R(ctx->dict), _R(valreg)))
		PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, keyreg))
		ERROR_RETURN_LOG(int, "Cannot allocate the key register");

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, valreg))
		ERROR_RETURN_LOG(int, "Cannot allocate the val register");

	return 0;
}


static inline int _parse(pss_comp_t* comp, pss_comp_value_t* buf, _service_ctx_t** ctx)
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
		else if(ahead[0]->type == PSS_COMP_LEX_TOKEN_RPARENTHESIS ||
				ahead[0]->type == PSS_COMP_LEX_TOKEN_LBRACE ||
			    ahead[0]->type == PSS_COMP_LEX_TOKEN_STRING)
		{
			/* This is a service interconnection statement, we parse it differently */
			if(*ctx == NULL && NULL == (*ctx = _service_ctx_new(comp, seg, buf->regs[0].id)))
				ERROR_RETURN_LOG(int, "Cannot create new service context for the service literal");

			//if(ERROR_CODE(int) == _parse_edge(*ctx))
			//	ERROR_RETURN_LOG(int, "Cannot parse the service context literal");
		}
		else if(ahead[0]->type == PSS_COMP_LEX_TOKEN_RBRACE) break;
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

int pss_comp_dict_parse(pss_comp_t* comp, pss_comp_value_t* buf)
{
	_service_ctx_t* ctx = NULL;

	int rc = _parse(comp, buf, &ctx);

	if(NULL != ctx && ERROR_CODE(int) == _service_ctx_free(ctx))
		ERROR_RETURN_LOG(int, "Cannot dispose the service context for current service literal");

	return rc;
}
