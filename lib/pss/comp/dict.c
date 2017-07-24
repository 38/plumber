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
	pss_bytecode_regid_t  rev_reg;/*!< The reverse edge set register */
	struct _servlet_t*    next;   /*!< The next servlet in the servlet set */
} _servlet_t;

/**
 * @brief The string node
 **/
typedef struct _string_node_t {
	char* str;   /*!< The actual string */
	struct _string_node_t* next; /*< The next pointetr in the list */
} _string_node_t;

/**
 * @brief represents a pending edge
 * @note this data structure represents source_node (source_pipe) -&gt; (destination_pipe) destination_node
 **/
typedef struct _pending_edge_t {
	enum {
		_INPUT,                /*!< input pipe */
		_OUTPUT,               /*!< a output pipe */
		_N2N                   /*!< a node to node pipe */
	} type;                    /*!< the type of this edge */
	const char* source_node;      /*!< the register id carries the source node */
	const char* destination_node; /*!< the register id carries the destiniation node */
	const char* source_pipe;      /*!< the source end of the pipe (a string id) */
	const char* destination_pipe; /*!< the destination end of the pipe (a string id) */
	uint32_t stack;            /*!< if this node is allocated on the stack */
	uint32_t level;            /*!< how many levels of nested block */
	struct _pending_edge_t* next; /*!< used in the pending edge linked list */
} _pending_edge_t;

/**
 * @brief represents a list of pending edges
 **/
typedef struct {
	_pending_edge_t* head;  /* !< the head of the list */
	_pending_edge_t* tail;  /* !< the tail of the list */
} _pending_list_t;

/**
 * @brief The compile time service context
 **/
typedef struct {
	pss_comp_t*             comp;                          /*!< The current compiler instance */
	pss_bytecode_segment_t* seg;                           /*!< The bytecode segment we are using */
	pss_bytecode_regid_t    dict;                          /*!< The register that holds the result dictionary */
	_servlet_t*             nodes[PSS_COMP_MAX_SERVLET];   /*!< The servlet hash table, which maps the servlet name to the edge adj list */
	_string_node_t*         strings;                       /*!< The string list */
	uint32_t                level;                         /*!< The current level */
	const char*             right_node;                    /*!< The right node name */
} _service_ctx_t;

/**
 * @brief Compute the hash code
 * @param str The string to compute
 * @param len The length of the string
 * @param full The full 128bit hash code
 * @return The hash slot
 **/
uint32_t _hashcode(const char* str, size_t len, uint64_t full[2])
{
	murmurhash3_128(str, len, 0x1234567u, full);

	const uint32_t k = ((uint32_t)(0x8000000000000000ull%PSS_COMP_MAX_SERVLET) * 2) % PSS_COMP_MAX_SERVLET;

	return (uint32_t)((full[0] % PSS_COMP_MAX_SERVLET + (full[1] % PSS_COMP_MAX_SERVLET) * k) % PSS_COMP_MAX_SERVLET);
}

/**
 * @brief Get the adj list register by the node name
 * @param ctx The context
 * @param name The name
 * @return The regsister
 **/
pss_bytecode_regid_t _get_adj_reg(_service_ctx_t* ctx, const char* name, int rev)
{
	uint64_t hash[2];
	size_t len;
	uint32_t slot = _hashcode(name, len = strlen(name), hash);
	_servlet_t* ptr;
	for(ptr = ctx->nodes[slot]; NULL != ptr && (ptr->hash[0] != hash[0] || ptr->hash[1] != hash[1]) && strcmp(ptr->name, name) != 0; ptr = ptr->next);

	if(NULL != ptr)
	    return rev ? ptr->rev_reg : ptr->reg;

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

	if(ERROR_CODE(pss_bytecode_regid_t) == (node->rev_reg = pss_comp_mktmp(ctx->comp)))
	    ERROR_LOG_ERRNO_GOTO(CREATE_ERR, "Cannot allocate register for the reverse adj list");

	if(!_INST(ctx->seg, DICT_NEW, _R(node->rev_reg)))
	{
		pss_comp_raise_internal(ctx->comp, PSS_COMP_INTERNAL_CODE);
		goto CREATE_ERR;
	}

	node->hash[0] = hash[0];
	node->hash[1] = hash[1];

	if(NULL == (node->name = (char*)malloc(len + 1)))
	    ERROR_LOG_ERRNO_GOTO(CREATE_ERR, "Cannot allocate memory for the node name");

	memcpy(node->name, name, len + 1);

	node->next = ctx->nodes[slot];
	ctx->nodes[slot] = node;
	return rev ? node->rev_reg : node->reg;
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

			pss_bytecode_regid_t regs[] = {this->reg, this->rev_reg};
			const char*          pref[] = {"", "!"};

			uint32_t j;
			for(j = 0; j < 2; j ++)
			{
				char buf[1024];
				snprintf(buf, sizeof(buf), "%s@%s", pref[j], this->name);
				pss_bytecode_regid_t regid = pss_comp_mktmp(ctx->comp);
				if(ERROR_CODE(pss_bytecode_regid_t) == regid)
				    rc = ERROR_CODE(int);
				else do {
					if(!_INST(ctx->seg, STR_LOAD, _S(buf), _R(regid)))
					    goto ERR;
					if(!_INST(ctx->seg, SET_VAL, _R(regs[j]), _R(ctx->dict), _R(regid)))
					    goto ERR;
					if(ERROR_CODE(int) == pss_comp_rmtmp(ctx->comp, regid))
					    goto ERR;
					break;
ERR:
					pss_comp_raise_internal(ctx->comp, PSS_COMP_INTERNAL_CODE);
					rc = ERROR_CODE(int);
				} while(0);

				if(ERROR_CODE(pss_bytecode_regid_t) != regs[j] && ERROR_CODE(int) == pss_comp_rmtmp(ctx->comp, regs[j]))
				    rc = ERROR_CODE(int);
			}

			if(NULL != this->name) free(this->name);
			free(this);
		}
	}

	_string_node_t* ptr;
	for(ptr = ctx->strings; NULL != ptr; )
	{
		_string_node_t* this = ptr;
		ptr = ptr->next;
		free(this->str);
		free(this);
	}

	free(ctx);

	return rc;
}
static inline int _append_adj_list(_service_ctx_t* ctx, pss_bytecode_regid_t list_reg,
                                   const char* left_node, const char* left_port,
                                   const char* right_port, const char* right_node)
{
	(void)left_node;
	pss_comp_t* comp = ctx->comp;
	pss_bytecode_segment_t* seg = ctx->seg;

	char keybuf[1024];
	snprintf(keybuf, sizeof(keybuf), "%s", left_port);
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

	if(!_INST(seg, SET_VAL, _R(valreg), _R(list_reg), _R(keyreg)))
	    PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, keyreg))
	    ERROR_RETURN_LOG(int, "Cannot release the key register");

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, valreg))
	    ERROR_RETURN_LOG(int, "Cannot release the val register");

	return 0;
}
static inline int _add_edge(_service_ctx_t* ctx, const char* left_node, const char* left_port, const char* right_port, const char* right_node)
{
	pss_comp_t* comp = ctx->comp;

	if(NULL == left_node || NULL == right_node || NULL == left_port || NULL == right_port)
	    PSS_COMP_RAISE_INT(comp, ARGS);

	pss_bytecode_regid_t list_reg = _get_adj_reg(ctx, left_node, 0);
	if(ERROR_CODE(pss_bytecode_regid_t) == list_reg)
	    ERROR_RETURN_LOG(int, "Cannot get the register for the adj list");

	if(ERROR_CODE(int) == _append_adj_list(ctx, list_reg, left_node, left_port, right_port, right_node))
	    ERROR_RETURN_LOG(int, "Cannot put the edge to the adj list");

	pss_bytecode_regid_t list_reg_rev = _get_adj_reg(ctx, right_node, 1);
	if(ERROR_CODE(pss_bytecode_regid_t) == list_reg_rev)
	    ERROR_RETURN_LOG(int, "Cannot get the register for the reverse adj list");

	if(ERROR_CODE(int) == _append_adj_list(ctx, list_reg_rev, right_node, right_port, left_port, left_node))
	    ERROR_RETURN_LOG(int, "Cannot put the reverse edge to the revers adj list");

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
	snprintf(valbuf, sizeof(valbuf), "%s@%s", node, port);

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

	if(!_INST(seg, SET_VAL, _R(valreg), _R(ctx->dict), _R(keyreg)))
	    PSS_COMP_RAISE_INT(comp, CODE);

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, keyreg))
	    ERROR_RETURN_LOG(int, "Cannot allocate the key register");

	if(ERROR_CODE(int) == pss_comp_rmtmp(comp, valreg))
	    ERROR_RETURN_LOG(int, "Cannot allocate the val register");

	return 0;
}

static inline void _pending_list_free(_pending_list_t* list)
{
	_pending_edge_t* tmp;
	for(;list->head != NULL;)
	{
		tmp = list->head;
		list->head = list->head->next;
		if(!tmp->stack) free(tmp);
	}
}
static inline int _pipe_block(_service_ctx_t* context, _pending_list_t* result, const char* left_node);

static inline void _pending_list_merge(_pending_list_t* first, _pending_list_t* second)
{
	if(first->head == NULL)
	    *first = *second;
	else if(second->head != NULL)
	    first->tail->next = second->head, first->tail = second->tail;
	second->head = second->tail = NULL;
}

static inline int _process_pending_list(_service_ctx_t* context, _pending_list_t* list, const char* left_node, const char* right_node)
{
	_pending_list_t out = {NULL, NULL};
	for(; NULL != list->head; )
	{
		_pending_edge_t* ptr = list->head;
		list->head = list->head->next;

		/* Update the undetermined node */
		if(ptr->type == _N2N)
		{
			if(ptr->source_node == NULL) ptr->source_node = left_node;
			if(ptr->destination_node == NULL) ptr->destination_node = right_node;
		}
		else if(ptr->type == _INPUT)
		{
			/**
			 * For a input pipe, if current level is the same level, which means this is in the same statement
			 * this must be things like () -> "pipe0" node0 "pipe1" ..... In this case, the destination node
			 * for this input should be the left node.
			 * However if the level is different, which means we have things like
			 * 		{ () -> "pipe" } node
			 * In this case, we should use the right node to populate the destination node
			 **/
			if(ptr->destination_node == NULL)
			    ptr->destination_node = (context->level == ptr->level) ? left_node : right_node;
		}
		else if(ptr->type == _OUTPUT)
		{
			if(ptr->source_node == NULL)
			    ptr->source_node = (context->level == ptr->level) ? right_node : left_node;
		}

		/* Then if the node is ready to compile, produce the instructions */
		if(ptr->type == _N2N && ptr->source_node != NULL && ptr->destination_node != NULL)
		{
			if(ERROR_CODE(int) == _add_edge(context, ptr->source_node, ptr->source_pipe, ptr->destination_pipe, ptr->destination_node))
			    ERROR_LOG_GOTO(LOOP_ERR, "Cannot add node to the context");
			if(!ptr->stack) free(ptr);
		}
		else if(ptr->type == _INPUT && ptr->destination_node  != NULL)
		{
			if(ERROR_CODE(int) == _add_port(context, ptr->destination_node, ptr->destination_pipe, ptr->source_pipe, 1))
			    ERROR_LOG_GOTO(LOOP_ERR, "Cannot add node to the context");
			if(!ptr->stack) free(ptr);
		}
		else if(ptr->type == _OUTPUT && ptr->source_node != NULL)
		{
			if(ERROR_CODE(int) == _add_port(context, ptr->source_node, ptr->source_pipe, ptr->destination_pipe, 0))
			    ERROR_LOG_GOTO(LOOP_ERR, "Cannot add node to context");
			if(!ptr->stack) free(ptr);
		}
		else
		{
			if(ptr->stack)
			{
				_pending_edge_t* new_node = (_pending_edge_t*)malloc(sizeof(*new_node));
				if(NULL == new_node)
				{
					pss_comp_raise_internal(context->comp, PSS_COMP_INTERNAL_MALLOC);
					goto ERR;
				}
				*new_node = *ptr;
				ptr = new_node;
				ptr->stack = 0;
			}
			if(out.head == NULL) out.head = out.tail = ptr;
			else out.tail->next = ptr, ptr->next = NULL, out.tail = ptr;
		}
		continue;
LOOP_ERR:
		if(!ptr->stack) free(ptr);
		goto ERR;
	}
	*list = out;

	return 0;
ERR:
	_pending_list_free(list);
	_pending_list_free(&out);
	list->head = list->tail = NULL;
	return ERROR_CODE(int);
}


static inline const char* _get_str(_service_ctx_t* context, const char* str)
{
	_string_node_t* node = (_string_node_t*)malloc(sizeof(*node));
	if(NULL == node)
	{
		pss_comp_raise_internal(context->comp, PSS_COMP_INTERNAL_MALLOC);
		return NULL;
	}

	if(NULL == (node->str = strdup(str)))
	{
		free(node);
		pss_comp_raise_internal(context->comp, PSS_COMP_INTERNAL_MALLOC);
		return NULL;
	}

	node->next = context->strings;
	context->strings = node;

	return node->str;
}

static inline const char* _consume_and_store(_service_ctx_t* ctx)
{
	const pss_comp_lex_token_t* ahead = pss_comp_peek(ctx->comp, 0);
	if(NULL == ahead)
	    ERROR_PTR_RETURN_LOG("Cannot peek the token ahead");

	if(ahead->type != PSS_COMP_LEX_TOKEN_IDENTIFIER &&
	   ahead->type != PSS_COMP_LEX_TOKEN_STRING)
	    PSS_COMP_RAISE_SYNT_PTR(ctx->comp, "Unexpected token type");
	const char* ret = _get_str(ctx, ahead->value.s);

	if(ERROR_CODE(int) == pss_comp_consume(ctx->comp, 1))
	    ERROR_PTR_RETURN_LOG("Cannot consume the parsed token");

	return ret;
}


static inline int _unbounded_chain(_service_ctx_t* context, _pending_list_t* result, const char* left_node, int allow_empty)
{
	_pending_edge_t temp;
	_pending_list_t childres = {NULL, NULL};
	int empty = !allow_empty;
	for(;;)
	{
		const pss_comp_lex_token_t* token0 = pss_comp_peek(context->comp, 0);
		const pss_comp_lex_token_t* token1 = pss_comp_peek(context->comp, 1);
		const pss_comp_lex_token_t* token2 = pss_comp_peek(context->comp, 2);
		const char* right_node = NULL;
		if(NULL == token0 || NULL == token1 || NULL == token2)
		    ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

		if(PSS_COMP_LEX_TOKEN_STRING == token0->type && PSS_COMP_LEX_TOKEN_ARROW == token1->type && PSS_COMP_LEX_TOKEN_STRING == token2->type)
		{
			/* if the first token is string means the first edge is "pipe" -> "pipe" */

			const char* source_pipe = _get_str(context, token0->value.s);
			if(NULL == source_pipe) goto ERR;

			const char* dest_pipe = _get_str(context, token2->value.s);
			if(NULL == dest_pipe) goto ERR;
			if(ERROR_CODE(int) == pss_comp_consume(context->comp, 3))
			    ERROR_LOG_GOTO(ERR, "Cannot consume the token ahead");

			temp.type = _N2N;
			temp.source_node = left_node;
			temp.source_pipe = source_pipe;
			temp.destination_pipe = dest_pipe;
			temp.destination_node = NULL;
			temp.level = context->level;
			temp.next = NULL;
			temp.stack = 1;
			childres.head = &temp;
			childres.tail = &temp;
			empty = 0;
		}
		else if(PSS_COMP_LEX_TOKEN_LBRACE == token0->type)
		{
			childres.head = childres.tail = NULL;
			if(ERROR_CODE(int) == _pipe_block(context, &childres, left_node))
			    ERROR_LOG_GOTO(ERR, "Invalid pipe description block");
			empty = 0;
		}
		else break;

		token0 = pss_comp_peek(context->comp, 0);
		if(PSS_COMP_LEX_TOKEN_IDENTIFIER == token0->type && (NULL == (right_node = _consume_and_store(context))))
		    ERROR_LOG_GOTO(ERR, "Cannot parse the node name");

		if(ERROR_CODE(int) == _process_pending_list(context, &childres, left_node, right_node))
		    ERROR_LOG_GOTO(ERR, "Cannot process the pending list");

		_pending_list_merge(result, &childres);

		context->right_node = right_node;

		left_node = right_node;

		if(left_node == NULL) break;
	}

	if(empty)
	    PSS_COMP_RAISE_SYN_GOTO(ERR, context->comp, "Empty pipe statement is not allowed");

	return 0;
ERR:
	_pending_list_free(result);
	_pending_list_free(&childres);
	return ERROR_CODE(int);
}

static inline int _pipe_input(_service_ctx_t* context, _pending_list_t* result, _pending_edge_t* buf)
{
	if(ERROR_CODE(int) == pss_comp_consume(context->comp, 1))
	    ERROR_RETURN_LOG(int, "Cannot consume the token");

	const char* name = NULL;
	const pss_comp_lex_token_t* ahead = pss_comp_peek(context->comp, 0);
	if(NULL == ahead)
	    ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

	if(PSS_COMP_LEX_TOKEN_IDENTIFIER == ahead->type)
	{
		if(NULL == (name = _consume_and_store(context)))
		    ERROR_RETURN_LOG(int, "Cannot store the virtual port name");
		if(NULL == (ahead = pss_comp_peek(context->comp, 0)))
		    ERROR_RETURN_LOG(int, "Cannot peek the ahead token");
	}

	const pss_comp_lex_token_t* ahead1 = pss_comp_peek(context->comp, 1);
	if(NULL == ahead1) ERROR_RETURN_LOG(int, "Cannot peek the next token");

	if(PSS_COMP_LEX_TOKEN_RPARENTHESIS != ahead->type || PSS_COMP_LEX_TOKEN_ARROW != ahead1->type)
	    ERROR_RETURN_LOG(int, "Either input port or named port is expected");
	if(ERROR_CODE(int) == pss_comp_consume(context->comp, 2))
	    ERROR_RETURN_LOG(int, "Cannot consume parsed token");

	const pss_comp_lex_token_t* token = pss_comp_peek(context->comp, 0);

	if(PSS_COMP_LEX_TOKEN_STRING != token->type)
	    PSS_COMP_RAISE_SYN(int, context->comp, "Pipe name is expected");

	const char* pipe = NULL;
	if(NULL == (pipe = _consume_and_store(context)))
	    ERROR_RETURN_LOG(int, "Cannot store the pipe name");

	buf->type = _INPUT;
	buf->source_pipe = name;
	buf->destination_node = NULL;
	buf->destination_pipe = pipe;
	buf->stack = 1;
	buf->level = context->level;
	buf->next = NULL;
	result->head = result->tail = buf;
	return 0;
}

static inline int _pipe_output(_service_ctx_t* context, _pending_list_t* result, _pending_edge_t* buf, const char* left_node)
{
	const pss_comp_lex_token_t* pipe_token = pss_comp_peek(context->comp, 0);
	if(NULL == pipe_token) ERROR_RETURN_LOG(int, "Cannot peek the ahead token");
	const char* pipe = NULL;
	if(NULL == (pipe = _get_str(context, pipe_token->value.s)))
	    ERROR_RETURN_LOG(int, "Cannot store the pipe name");

	/* we assume that the caller has alread examined it must be a output pipe */
	if(ERROR_CODE(int) == pss_comp_consume(context->comp, 3))
	    ERROR_RETURN_LOG(int, "Cannot consume the token");

	const char* name = NULL;
	const pss_comp_lex_token_t* ahead = pss_comp_peek(context->comp, 0);
	if(NULL == ahead) ERROR_RETURN_LOG(int, "Cannot peek the ahead token");
	if(ahead->type == PSS_COMP_LEX_TOKEN_IDENTIFIER)
	{
		if(NULL == (name = _consume_and_store(context)))
		    ERROR_RETURN_LOG(int, "Cannot store the output port name");
	}

	if(NULL == (ahead = pss_comp_peek(context->comp, 0))) ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

	if(ahead->type == PSS_COMP_LEX_TOKEN_RPARENTHESIS)
	{
		if(ERROR_CODE(int) == pss_comp_consume(context->comp, 1))
		    ERROR_RETURN_LOG(int, "Cannot consume the parsed token");
	}
	else PSS_COMP_RAISE_SYN(int, context->comp, "Token right parenthesis expected");

	buf->type = _OUTPUT;
	buf->source_node = left_node;
	buf->source_pipe = pipe;
	buf->destination_pipe = name;
	buf->stack = 1;
	buf->level = context->level;
	buf->next = NULL;
	result->head = result->tail = buf;

	return 0;
}


static inline int _pipe_statement(_service_ctx_t* context, _pending_list_t* result, const char* left_node)
{
	int empty = 1;
	const pss_comp_lex_token_t* token = pss_comp_peek(context->comp, 0);
	if(NULL == token) ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

	_pending_list_t input_list = {NULL, NULL};
	_pending_edge_t input_edge;

	if(PSS_COMP_LEX_TOKEN_LPARENTHESIS == token->type)
	{
		empty = 0;
		/* If there's an input pipe, we should parse the input pipe */
		if(ERROR_CODE(int) == _pipe_input(context, &input_list, &input_edge))
		    ERROR_RETURN_LOG(int, "Canont parse the input pipe");

		_pending_list_merge(result, &input_list);
		/* Because to token has been alread consumed by _service_graph_pipe_input, so update the token */
		token = pss_comp_peek(context->comp, 0);
		if(NULL == token) ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

		if(PSS_COMP_LEX_TOKEN_IDENTIFIER != token->type)  /* if this is only a () -> "pipe" */
		{
			if(ERROR_CODE(int) == _process_pending_list(context, result, left_node, NULL))
			    ERROR_RETURN_LOG(int, "Cannot process the pending list");
			return 0;
		}
	}

	if(PSS_COMP_LEX_TOKEN_IDENTIFIER == token->type && NULL == (left_node = _consume_and_store(context)))
	    ERROR_RETURN_LOG(int, "Cannot store the servlet name");

	const pss_comp_lex_token_t* ahead[3] = {
		pss_comp_peek(context->comp, 0),
		pss_comp_peek(context->comp, 1),
		pss_comp_peek(context->comp, 2)
	};

	if(NULL == ahead[0] || NULL == ahead[1] || NULL == ahead[2])
	    ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

	if(ahead[0]->type == PSS_COMP_LEX_TOKEN_STRING &&
	   ahead[1]->type == PSS_COMP_LEX_TOKEN_ARROW  &&
	   ahead[2]->type == PSS_COMP_LEX_TOKEN_LPARENTHESIS)
	{
		/* if this statement is only a "pipe" -> () */
		_pending_list_t output_list = {NULL, NULL};
		_pending_edge_t output_edge;
		if(_pipe_output(context, &output_list, &output_edge, left_node) == ERROR_CODE(int))
		    ERROR_RETURN_LOG(int, "Invalid output pipe desc");

		_pending_list_merge(result, &output_list);

		if(ERROR_CODE(int) == _process_pending_list(context, result, left_node, NULL))
		    ERROR_RETURN_LOG(int, "Cannot process the pending list");
		return 0;
	}

	context->right_node = NULL;

	/* this statement do not have a left node, if parser previously has shifted, we can allow a empty chain */
	if(ERROR_CODE(int) == _unbounded_chain(context, result, left_node, !empty))
	    ERROR_RETURN_LOG(int, "Invalid pipe chain");

	if(NULL == (ahead[0] = pss_comp_peek(context->comp, 0)) ||
	   NULL == (ahead[1] = pss_comp_peek(context->comp, 1)) ||
	   NULL == (ahead[2] = pss_comp_peek(context->comp, 2)))
	    ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

	const char* right_node = context->right_node;

	if(ahead[0]->type == PSS_COMP_LEX_TOKEN_STRING &&
	   ahead[1]->type == PSS_COMP_LEX_TOKEN_ARROW  &&
	   ahead[2]->type == PSS_COMP_LEX_TOKEN_LPARENTHESIS)
	{
		/* Basically, it's impossible that the pipe definition like
		 *     () -> "xxx" node "yyy" -> ()
		 * can fell to this, because we take this special case before processing the chain.
		 * And this is only valid form that a chain be empty
		 * So if the chain is empty, we won't be here. So the last position of the parser will remains
		 * at () -> "xxxx", which means Ok. And what we actually want to do here is to prevent
		 * parser accept a 0 length statement, which causes the parser fell in to a infinite loop */
		if(right_node == NULL)
		    PSS_COMP_RAISE_SYN(int, context->comp, "Unspecified output node");

		/* process the output pipe */
		_pending_list_t output_list = {NULL, NULL};
		_pending_edge_t output_edge;
		if(_pipe_output(context, &output_list, &output_edge, right_node) == ERROR_CODE(int))
		    ERROR_RETURN_LOG(int, "Invalid output pipe desc");
		_pending_list_merge(result, &output_list);
	}

	if(ERROR_CODE(int) == _process_pending_list(context, result, left_node, right_node))
	    ERROR_RETURN_LOG(int, "Cannot process the pending list");

	return 0;
}

static inline int _pipe_block(_service_ctx_t* context, _pending_list_t* result, const char* left_node)
{
	context->level ++;
	if(ERROR_CODE(int) == pss_comp_consume(context->comp, 1))
	    ERROR_RETURN_LOG(int, "Cannot consume the ahead token");
	for(;;)
	{
		const pss_comp_lex_token_t* start = pss_comp_peek(context->comp, 0);
		if(NULL == start) ERROR_RETURN_LOG(int, "Cannot peek the ahead token");

		_pending_list_t child = {NULL, NULL};

		if(PSS_COMP_LEX_TOKEN_RBRACE == start->type)
		{
			if(ERROR_CODE(int) == pss_comp_consume(context->comp, 1))
			    ERROR_RETURN_LOG(int, "Cannot consume the parsed token");
			break;
		}
		if(PSS_COMP_LEX_TOKEN_IDENTIFIER != start->type &&
		   PSS_COMP_LEX_TOKEN_LPARENTHESIS != start->type &&
		   PSS_COMP_LEX_TOKEN_LBRACE != start->type &&
		   PSS_COMP_LEX_TOKEN_STRING != start->type)
		    PSS_COMP_RAISE_SYN(int, context->comp, "Pipe statment expected");

		if(ERROR_CODE(int) == _pipe_statement(context, &child, left_node))
		    ERROR_RETURN_LOG(int, "Invalid pipe statement");

		_pending_list_merge(result, &child);

		if(NULL == (start = pss_comp_peek(context->comp, 0)))
		    ERROR_RETURN_LOG(int, "Cannot peek the next token");

		if(start->type == PSS_COMP_LEX_TOKEN_SEMICOLON)
		{
			if(ERROR_CODE(int) == pss_comp_consume(context->comp, 1))
			    ERROR_RETURN_LOG(int, "Cannot consume the next token");
		}
	}

	context->level --;
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
		else if(ahead[0]->type == PSS_COMP_LEX_TOKEN_LPARENTHESIS ||
		        ahead[0]->type == PSS_COMP_LEX_TOKEN_LBRACE ||
		        ahead[0]->type == PSS_COMP_LEX_TOKEN_IDENTIFIER)
		{
			/* This is a service interconnection statement, we parse it differently */
			if(*ctx == NULL && NULL == (*ctx = _service_ctx_new(comp, seg, buf->regs[0].id)))
			    ERROR_RETURN_LOG(int, "Cannot create new service context for the service literal");

			_pending_list_t list = {NULL, NULL};
			if(ERROR_CODE(int) == _pipe_statement(*ctx, &list, NULL))
			    ERROR_RETURN_LOG(int, "Cannot parse the pipe statement");
			else if(NULL != list.head)
			{
				_pending_list_free(&list);
				PSS_COMP_RAISE_SYN(int, comp, "Invalid pending edge in the top scope, a servlet node missing?");
			}
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
