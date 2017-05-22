/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <plumber.h>
#include <error.h>
#include <utils/bitmask.h>
#include <utils/log.h>
#include <utils/hashmap.h>
#include <utils/vector.h>

/**
 * @brief the place holder that indicates the left node is undetermind
 **/
#define _UNDETERMINED ERROR_CODE(uint32_t)

/**
 * @brief the initializer for a operand
 * @param opt the operand type
 * @param opf the operand field
 * @param opv the operand value
 **/
#define _OP(opt, opf, opv) {\
	.type = LANG_BYTECODE_OPERAND_##opt,\
	.opf = opv\
}
/**
 * @brief output a unary instruction
 * @param inst the instruction name
 * @param op1 the first operand
 * @param on_error the action for the error cases
 **/
#define _INS_1OP(inst, op1, on_error) \
    do {\
	    lang_bytecode_operand_t operand_1 = op1;\
	    if(lang_bytecode_table_append_##inst(compiler->bc_tab, &operand_1) == ERROR_CODE(int))\
	    {\
		    LOG_ERROR("Cannot append "#inst" instruction to the bytecode table");\
		    _set_error(compiler, "Internal error");\
		    on_error;\
	    }\
    }while(0)

/**
 * @brief output a binary instruction
 * @param inst the instruction name
 * @param op1 the first operand
 * @param op2 the second operand
 * @param on_error the action for the error cases
 **/
#define _INS_2OP(inst, op1, op2, on_error) \
    do {\
	    lang_bytecode_operand_t operand_1 = op1;\
	    lang_bytecode_operand_t operand_2 = op2;\
	    if(lang_bytecode_table_append_##inst(compiler->bc_tab, &operand_1, &operand_2) == ERROR_CODE(int))\
	    {\
		    LOG_ERROR("Cannot append "#inst" instruction to the bytecode table");\
		    _set_error(compiler, "Internal error");\
		    on_error;\
	    }\
    }while(0)


/**
 * @brief output an arithemtic instruction
 * @param type the type of the instruction
 * @param dest the destination register
 * @param op1 the left operand
 * @param op2 the right operand
 * @param on_error the action for error cases
 **/
#define _INS_ARITHMETIC(type, dest, op1, op2, on_error) \
    do {\
	    lang_bytecode_operand_t operand_1 = dest;\
	    lang_bytecode_operand_t operand_2 = op1;\
	    lang_bytecode_operand_t operand_3 = op2;\
	    if(lang_bytecode_table_append_arithmetic(compiler->bc_tab, type, &operand_1, &operand_2, &operand_3) == ERROR_CODE(int))\
	    {\
		    LOG_ERROR("Cannot append arithemtic instruction to the bytecode table");\
		    _set_error(compiler, "Internal error");\
		    on_error;\
	    }\
    } while(0);

/**
 * @brief set compiler into error state then perform an action
 * @param message the error message
 * @param action the action to take after that
 **/
#define _COMPILE_ERROR(message, action) do{\
	_set_error(compiler, message);\
	action;\
} while(0)

/**
 * @brief set the compiler into interal error state
 * @param action the action after that
 **/
#define _INTERNAL_ERROR(action) _COMPILE_ERROR("Internal error", action)

/**
 * @brief set the compiler into internal error state, log an error message and take an action
 * @param msg the message to output
 * @param action the action after that
 **/
#define _INTERNAL_ERROR_LOG(msg, action) _INTERNAL_ERROR(LOG_ERROR(msg); action)

/**
 * @brief allocate a unused register
 * @param name the output variable name
 * @param onerror the action when error happens
 **/
#define _REGALLOC(name, onerror) \
    if(ERROR_CODE(uint32_t) == (name = (uint32_t)bitmask_alloc(compiler->regmap)))\
    {\
	    LOG_ERROR("Cannot allocate "#name" register");\
	    onerror;\
    }

/**
 * @brief free a used register
 * @param name the register variable
 * @param onerror the action when error happens
 **/
#define _REGFREE(name, onerror) \
    if(ERROR_CODE(uint32_t) != name && ERROR_CODE(int) == bitmask_dealloc(compiler->regmap, name))\
    {\
	    LOG_ERROR("Cannot release "#name" register");\
	    onerror;\
    }

/**
 * @brief parse the symbol
 * @param var the variable that carries the symbol
 * @param parent the parent name
 * @param onerror the error action
 * @param lookup_parent if unset this flag, the default action for the undefined var should be use the current scope one.
 *        Otherwise, we need to find in the parent scope
 **/
#define _PARSE_SYM(var, parent, lookup_parent, onerror) do {\
	if(NULL == (var = _symbol(compiler, lookup_parent)))\
	{\
		_set_error(compiler, "Invalid symbol in "parent);\
		onerror;\
	}\
} while(0)

/**
 * @brief expect the n-th token
 * @param n the n-th token
 * @param token the token type expected
 * @param onerror the error action
 **/
#define _EXPECT_TOKEN(n, token, onerror) do{\
	if(_peek(compiler, (n))->type != LANG_LEX_TOKEN_##token)\
	{\
		_set_error(compiler, "Token "#token" expected");\
		onerror;\
	}\
} while(0)

/**
 * @brief the number of tokens in the lookahead buffer
 **/
#define _LOOKAHEAD_NUM 3

/**
 * @brief the information about current block
 **/
typedef struct _block_t {
	struct _block_t* parent;    /*!< the parent block */
	uint32_t block_id;          /*!< the index for this block */
} _block_t;

/**
 * @brief the actual data structure for a compiler instance
 **/
struct _lang_compiler_t {
	lang_lex_t*            lexer;         /*!< the lexer attached to this compiler */
	lang_bytecode_table_t* bc_tab;        /*!< the bytecode table used to store the results */
	bitmask_t*             regmap;        /*!< the register status bit map */
	lang_compiler_options_t options;      /*!< the compiler options */
	lang_compiler_error_t* error;         /*!< the error list */
	lang_lex_token_t       token_ahead[_LOOKAHEAD_NUM];/*!< the lookahead buffer */
	uint32_t               token_ahead_begin; /*!< where the lookahead buffer begins */
	hashmap_t*             vars;              /*!< the set for defined variables */
	_block_t*              current_block;     /*!< the current block */
	uint32_t               next_block_id;     /*!< the next block id that is avaiable */
	uint32_t               inc_level;         /*!< the include level */
	lang_bytecode_label_table_t* labels;      /*!< the label table */
	uint32_t               cont_label;        /*!< the label for continue */
	uint32_t               brk_label;         /*!< the label for break */
};

lang_compiler_t* lang_compiler_new(lang_lex_t* lexer, lang_bytecode_table_t* bc_table, lang_compiler_options_t options)
{
	if(NULL == lexer || NULL == bc_table) ERROR_PTR_RETURN_LOG("Invalid arguments");

	int i;
	lang_compiler_t* ret = (lang_compiler_t*)calloc(1, sizeof(lang_compiler_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory");

	ret->lexer = lexer;
	ret->bc_tab = bc_table;
	ret->options = options;
	ret->error = NULL;
	ret->regmap = bitmask_new(ret->options.reg_limit);
	ret->token_ahead_begin = 0;
	ret->current_block = NULL;
	ret->next_block_id = 0;
	ret->inc_level = 0;
	ret->cont_label = ERROR_CODE(uint32_t);
	ret->brk_label = ERROR_CODE(uint32_t);
	if(NULL == (ret->vars = hashmap_new(LANG_COMPILER_NODE_HASH_SIZE, LANG_COMPILER_NODE_HASH_POOL_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create the variable hashtable");

	if(NULL == (ret->labels = lang_bytecode_label_table_new()))
	    ERROR_LOG_GOTO(ERR, "Cannot create the label table");

	for(i = 0; i < _LOOKAHEAD_NUM; i ++)
	    ret->token_ahead[i].type = LANG_LEX_TOKEN_NAT;

	if(NULL == ret->regmap)
	    ERROR_LOG_GOTO(ERR, "Cannot create register map");

	LOG_DEBUG("Compiler instnace has been successfully created");

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->regmap)
		{
			bitmask_free(ret->regmap);
		}
		if(NULL != ret->vars)
		{
			hashmap_free(ret->vars);
		}
		if(NULL != ret->labels)
		{
			lang_bytecode_label_table_free(ret->labels);
		}
		free(ret);
	}
	return NULL;
}

int lang_compiler_free(lang_compiler_t* compiler)
{
	if(NULL == compiler) ERROR_RETURN_LOG(int, "Invalid arguments");

	/* deallocate the error messages */
	lang_compiler_error_t* err;
	for(err = compiler->error; NULL != err;)
	{
		lang_compiler_error_t* tmp = err;
		err = err->next;
		free(tmp);
	}

	/* deallocate the block information */
	_block_t* block;
	for(block = compiler->current_block; NULL != block;)
	{
		_block_t* tmp = block;
		block = block->parent;
		free(tmp);
	}

	int rc = 0;

	if(NULL != compiler->regmap && bitmask_free(compiler->regmap) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(NULL != compiler->vars && hashmap_free(compiler->vars) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(NULL != compiler->labels && lang_bytecode_label_table_free(compiler->labels) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	free(compiler);
	return rc;
}

/**
 * @brief define a new label
 * @param compiler the compiler context
 * @return the label id or error code
 **/
static inline uint32_t _new_label(lang_compiler_t* compiler)
{
	return lang_bytecode_label_table_new_label(compiler->labels);
}

/**
 * @brief mark current location as the target of the label
 * @param compiler the compiler context
 * @param labelid the label id
 * @return status code
 **/
static inline int _set_label_target(lang_compiler_t* compiler, uint32_t labelid)
{
	return lang_bytecode_label_table_assign_current_address(compiler->labels, compiler->bc_tab, labelid);
}

/**
 * @brief look ahead the tokens
 * @param compiler the target compiler
 * @param n the offset of the token against current location
 * @return the result token
 **/
static inline const lang_lex_token_t* _peek(lang_compiler_t* compiler, int n)
{
	if(n >= _LOOKAHEAD_NUM || n < 0)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");
	uint32_t i;
	for(i = 0; i <= (uint32_t)n; i ++)
	{
		uint32_t off = (i + compiler->token_ahead_begin) % _LOOKAHEAD_NUM;
		if(compiler->token_ahead[off].type == LANG_LEX_TOKEN_NAT)
		{
			if(lang_lex_next_token(compiler->lexer, compiler->token_ahead + off) == ERROR_CODE(int))
			    ERROR_PTR_RETURN_LOG("Failed to peek next token");
		}
	}
	return compiler->token_ahead + ((uint32_t)n + compiler->token_ahead_begin) % _LOOKAHEAD_NUM;
}
/**
 * @brief consume N tokens
 * @param compiler the compiler instance
 * @param n how many tokens to consume
 * @return nothing
 */
static inline void _consume(lang_compiler_t* compiler, int n)
{
	if(n <= 0)
	{
		LOG_WARNING("Invalid consume number %d, ignored", n);
		return;
	}
	else if(n > _LOOKAHEAD_NUM)
	{
		LOG_WARNING("Invalid argument, cosume number is larger than the look head number, which means"
		            "Some token has been cosumed silently.");
		LOG_WARNING("Changing the consume number to %d", _LOOKAHEAD_NUM);
		n = _LOOKAHEAD_NUM;
	}
	uint32_t i;
	for(i = 0; i < (uint32_t)n; i ++)
	    compiler->token_ahead[(i + compiler->token_ahead_begin) % _LOOKAHEAD_NUM].type = LANG_LEX_TOKEN_NAT;
	compiler->token_ahead_begin = (compiler->token_ahead_begin + (uint32_t)n) % _LOOKAHEAD_NUM;
}

/**
 * @brief set the compiler to error state
 * @param compiler the compiler context
 * @param msg the error message
 * @return nothing
 **/
static inline void _set_error(lang_compiler_t* compiler, const char* msg)
{
	lang_compiler_error_t* error = (lang_compiler_error_t*)malloc(sizeof(lang_compiler_error_t));
	if(NULL == error)
	{
		LOG_ERROR_ERRNO("Cannot allocate memory");
		return;
	}

	const lang_lex_token_t* token = compiler->token_ahead + compiler->token_ahead_begin;

	error->file = token->file;
	error->line = token->line;
	error->off  = token->offset;
	if(token->type != LANG_LEX_TOKEN_ERROR)
	    error->message = msg;
	else
	    error->message = token->value.e;
	error->next = compiler->error;
	compiler->error = error;
}

/**
 * @brief parse a symbol like a.b.c
 * @param compiler the compiler context
 * @param lookup_parent indicates if we want to lookup it in the parent scope if the variable is currently not defined
 * @return the parsed symbol string id array
 **/
static inline uint32_t* _symbol(lang_compiler_t* compiler, int lookup_parent)
{
	uint32_t cap = 32, size = 0;
	uint32_t* ret = (uint32_t*)malloc(sizeof(uint32_t) * cap);
	if(NULL == ret)
	    _INTERNAL_ERROR(ERROR_PTR_RETURN_LOG_ERRNO("cannot allocate memory"));

	for(;;)
	{
		const lang_lex_token_t* token = _peek(compiler, 0);
		if(token->type != LANG_LEX_TOKEN_IDENTIFIER)
		    _COMPILE_ERROR("identifier expected",
		        free(ret);
		        return NULL;
		    );

		if(cap <= size + 1)  /* Because we need to preserve one more slot for the end sign */
		{
			uint32_t* tmp = (uint32_t*)realloc(ret, sizeof(uint32_t) * cap * 2);
			if(NULL == tmp)
			    _INTERNAL_ERROR(free(ret); ERROR_PTR_RETURN_LOG_ERRNO("cannot resize the symbol buffer"));
			ret = tmp;
			cap = cap * 2;
		}

		if(ERROR_CODE(uint32_t) == (ret[size++] = lang_bytecode_table_acquire_string_id(compiler->bc_tab, token->value.s)))
		    _INTERNAL_ERROR_LOG("cannot acquire the string id from the bytecode table", free(ret); return NULL);
		else
		    LOG_DEBUG("append identifer %s to symbol", token->value.s);

		_consume(compiler, 1);

		if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_DOT)
		{
			ret[size] = ERROR_CODE(uint32_t);
			//return ret;
			break;
		}

		_consume(compiler, 1);
	}

	/* handle the block variables */
	if(cap <= size + 1)
	{
		/* the block descriptor occupies at most 1 segment, so we should allocate one more slot for it */
		uint32_t* tmp = (uint32_t*)realloc(ret, sizeof(uint32_t) * (cap + 1));
		if(NULL == tmp)
		    _INTERNAL_ERROR(free(ret); ERROR_PTR_RETURN_LOG_ERRNO("cannot resize the symbol buffer"));
		ret = tmp;
		cap ++;
	}
	const _block_t* block;
	for(block = compiler->current_block; NULL != block; block = block->parent)
	{
		char buf[10];
		string_buffer_t sbuf;
		string_buffer_open(buf, sizeof(buf), &sbuf);
		string_buffer_appendf(&sbuf, "@%x", block->block_id);
		const char* block_desc = string_buffer_close(&sbuf);

		if(ERROR_CODE(uint32_t) == (ret[size] = lang_bytecode_table_acquire_string_id(compiler->bc_tab, block_desc)))
		    _INTERNAL_ERROR(free(ret); ERROR_PTR_RETURN_LOG_ERRNO("Cannot acquire the symbol id for %s", block_desc));

		hashmap_find_res_t result;

		/* If there's a variable named this, means we do not need to search for more things
		   OR, If we do not want to search for the parent scope (for example, we are defining a local) */
		if(hashmap_find(compiler->vars, ret, (size + 1) * sizeof(uint32_t), &result) > 0 || !lookup_parent)
		{
			size ++;
			break;
		}
	}

	ret[size] = ERROR_CODE(uint32_t);
	return ret;
}

/**
 * @brief define a symbol to the vars table
 * @param compiler the compiler context
 * @param symbol the symbol array
 * @return status code
 **/
static inline int _define_symbol(lang_compiler_t* compiler, uint32_t* symbol)
{
	if(NULL == compiler || NULL == symbol) ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t len;
	for(len = 0; symbol[len] != ERROR_CODE(uint32_t); len ++);

	hashmap_find_res_t result;

	if(hashmap_insert(compiler->vars, symbol, len * sizeof(uint32_t), NULL, 0, &result, 0) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot add new symbol to the variable hash table");

	return 0;
}

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
	uint32_t source_node;      /*!< the register id carries the source node */
	uint32_t destination_node; /*!< the register id carries the destiniation node */
	uint32_t source_pipe;      /*!< the source end of the pipe (a string id) */
	uint32_t destination_pipe; /*!< the destination end of the pipe (a string id) */
	uint32_t stack;            /*!< if this node is allocated on the stack */
	uint32_t level;            /*!< how many levels of nested block */
	struct _pending_edge_t* next; /*!< used in the pending edge linked list */
} _service_graph_pending_edge_t;

/**
 * @brief represents a list of pending edges
 **/
typedef struct {
	_service_graph_pending_edge_t* head;  /* !< the head of the list */
	_service_graph_pending_edge_t* tail;  /* !< the tail of the list */
} _service_graph_pending_list_t;

/**
 * @brief the service graph compiler context
 **/
typedef struct {
	uint32_t   graph_reg;   /*!< the register for this graph */
	uint32_t   right_node;  /*!< the right most node for current chain */
	hashmap_t*  node_map;   /*!< the map from node name to register id */
	vector_t*   regs;       /*!< the registers used to hold the node */
	uint32_t   level;       /*!< current level */
} _service_graph_context_t;

static inline uint32_t _rvalue(lang_compiler_t* compiler);
static inline int _service_graph_pipe_block(lang_compiler_t* compiler, _service_graph_context_t* context,
                                            _service_graph_pending_list_t* result, uint32_t left_node);
static inline int _statement_list(lang_compiler_t* compiler);
static inline int _statement(lang_compiler_t* compiler);

/**
 * @brief dispose the pending list
 * @param list the pending list
 * @return nothing
 **/
static inline void _pending_list_free(_service_graph_pending_list_t* list)
{
	_service_graph_pending_edge_t* tmp;
	for(;list->head != NULL;)
	{
		tmp = list->head;
		list->head = list->head->next;
		if(!tmp->stack) free(tmp);
	}
}

/**
 * @brief merge two pending list
 * @param first the pointer to the first node of the first list
 * @param second the pointer to the first node of the second list
 * @return nothing
 **/
static inline void _pending_list_merge(_service_graph_pending_list_t* first, _service_graph_pending_list_t* second)
{
	if(first->head == NULL)
	    *first = *second;
	else if(second->head != NULL)
	    first->tail->next = second->head, first->tail = second->tail;
	second->head = second->tail = NULL;
}

/**
 * @brief process the pending list
 * @note this function reads the pending list and modify the pending list
 *       if copy flag has been set, the function will build a new list and DO NOT
 *       DEALLOCATE THE ORIGINAL LIST, make sure you pass this flag on a list that is allocated in stack!
 * @param compiler the compiler instance
 * @param context the context
 * @param list the pending list
 * @param left_node the left most node in the list
 * @param right_node the right most node for this list
 * @return status code
 **/
static inline int _process_pending_list(lang_compiler_t* compiler, _service_graph_context_t* context,
                                        _service_graph_pending_list_t* list, uint32_t left_node, uint32_t right_node)
{
	_service_graph_pending_list_t out = {NULL, NULL};
	uint32_t result_reg = ERROR_CODE(uint32_t);
	uint32_t sour_pipe_reg = ERROR_CODE(uint32_t);
	uint32_t dest_pipe_reg = ERROR_CODE(uint32_t);
	for(; NULL != list->head; )
	{
		_service_graph_pending_edge_t* ptr = list->head;
		list->head = list->head->next;

		/* Update the undetermined node */
		if(ptr->type == _N2N)
		{
			if(ptr->source_node == _UNDETERMINED) ptr->source_node = left_node;
			if(ptr->destination_node == _UNDETERMINED) ptr->destination_node = right_node;
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
			if(ptr->destination_node == _UNDETERMINED)
			    ptr->destination_node = (context->level == ptr->level) ? left_node : right_node;
		}
		else if(ptr->type == _OUTPUT)
		{
			if(ptr->source_node == _UNDETERMINED)
			    ptr->source_node = (context->level == ptr->level) ? right_node : left_node;
		}

		/* Then if the node is ready to compile, produce the instructions */
		if(ptr->type == _N2N && ptr->source_node != _UNDETERMINED && ptr->destination_node != _UNDETERMINED)
		{
			LOG_DEBUG("The pending node to node pipe is ready to compile");
			/* push the graph reg */
			_INS_1OP(pusharg, _OP(REG, reg, context->graph_reg), goto LOOP_ERR);
			/* push the source node */
			_INS_1OP(pusharg, _OP(REG, reg, ptr->source_node), goto LOOP_ERR);
			/* push the source pipe */
			_REGALLOC(sour_pipe_reg, goto LOOP_ERR);
			_INS_2OP(move, _OP(REG, reg, sour_pipe_reg), _OP(STRID, strid, ptr->source_pipe), goto LOOP_ERR);
			_INS_1OP(pusharg, _OP(REG, reg, sour_pipe_reg), goto LOOP_ERR);
			/*push the destination node */
			_INS_1OP(pusharg, _OP(REG, reg, ptr->destination_node), goto LOOP_ERR);
			_REGALLOC(dest_pipe_reg, goto LOOP_ERR);
			/*push the destination pipe */
			_INS_2OP(move, _OP(REG, reg, dest_pipe_reg), _OP(STRID, strid, ptr->destination_pipe), goto LOOP_ERR);
			_INS_1OP(pusharg, _OP(REG, reg, dest_pipe_reg), goto LOOP_ERR);
			/*invoke*/
			_REGALLOC(result_reg, goto LOOP_ERR);
			_INS_2OP(invoke, _OP(REG, reg, result_reg), _OP(BUILTIN, builtin, LANG_BYTECODE_BUILTIN_ADD_EDGE), goto LOOP_ERR);
			_REGFREE(result_reg, goto LOOP_ERR);
			_REGFREE(sour_pipe_reg, goto LOOP_ERR);
			_REGFREE(dest_pipe_reg, goto LOOP_ERR);
			if(!ptr->stack) free(ptr);
		}
		else if(ptr->type == _INPUT && ptr->destination_node  != _UNDETERMINED)
		{
			LOG_DEBUG("The pending input pipe is ready to compile");
			/* push the graph reg */
			_INS_1OP(pusharg, _OP(REG, reg, context->graph_reg), goto LOOP_ERR);
			/* push the destination node */
			_INS_1OP(pusharg, _OP(REG, reg, ptr->destination_node), goto LOOP_ERR);
			/* push the destination pipe */
			_REGALLOC(dest_pipe_reg, goto LOOP_ERR);
			_INS_2OP(move, _OP(REG,reg, dest_pipe_reg), _OP(STRID, strid, ptr->destination_pipe), goto LOOP_ERR);
			_INS_1OP(pusharg, _OP(REG, reg, dest_pipe_reg), goto LOOP_ERR);
			/* invoke */
			_REGALLOC(result_reg, goto LOOP_ERR);
			_INS_2OP(invoke, _OP(REG, reg, result_reg), _OP(BUILTIN, builtin, LANG_BYTECODE_BUILTIN_INPUT), goto LOOP_ERR);
			_REGFREE(result_reg, goto LOOP_ERR);
			_REGFREE(dest_pipe_reg, goto LOOP_ERR);
			if(!ptr->stack) free(ptr);
		}
		else if(ptr->type == _OUTPUT && ptr->source_node != _UNDETERMINED)
		{
			LOG_DEBUG("The pending output pipe is ready to compile");
			/* push the graph reg */
			_INS_1OP(pusharg, _OP(REG, reg, context->graph_reg), goto LOOP_ERR);
			/* push the source node */
			_INS_1OP(pusharg, _OP(REG, reg, ptr->source_node), goto LOOP_ERR);
			/* push the source pipe */
			_REGALLOC(sour_pipe_reg, goto LOOP_ERR);
			_INS_2OP(move, _OP(REG,reg, sour_pipe_reg), _OP(STRID, strid, ptr->source_pipe), goto LOOP_ERR);
			_INS_1OP(pusharg, _OP(REG, reg, sour_pipe_reg), goto LOOP_ERR);
			/* invoke */
			_REGALLOC(result_reg, goto LOOP_ERR);
			_INS_2OP(invoke, _OP(REG, reg, result_reg), _OP(BUILTIN, builtin, LANG_BYTECODE_BUILTIN_OUTPUT), goto LOOP_ERR);
			_REGFREE(result_reg, goto LOOP_ERR);
			_REGFREE(sour_pipe_reg, goto LOOP_ERR);
			if(!ptr->stack) free(ptr);
		}
		else
		{
			if(ptr->stack)
			{
				_service_graph_pending_edge_t* new_node = (_service_graph_pending_edge_t*)malloc(sizeof(_service_graph_pending_edge_t));
				if(NULL == new_node) _INTERNAL_ERROR_LOG("Cannot allocate memory", goto ERR);
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
		_REGFREE(sour_pipe_reg, /* NOP */);
		_REGFREE(dest_pipe_reg, /* NOP */);
		_REGFREE(result_reg, /* NOP */);
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

/**
 * @brief find the service graph node specified by the next token
 * @param compiler the compiler instance
 * @param context the graph context
 * @return the register reference for this servlet node
 **/
static inline uint32_t _service_graph_find_node(lang_compiler_t* compiler, _service_graph_context_t* context)
{
	const lang_lex_token_t* token = _peek(compiler, 0);

	hashmap_find_res_t reg;
	int rc;
	if(1 != (rc = hashmap_find(context->node_map, token->value.s, strlen(token->value.s) + 1, &reg)))
	{
		if(ERROR_CODE(int) == rc)
		    _INTERNAL_ERROR_LOG("Failed to query the servlet node hashmap", return ERROR_CODE(uint32_t));
		else
		    _COMPILE_ERROR("Undefined servlet node", return ERROR_CODE(uint32_t));
	}

	_consume(compiler, 1);

	return *(uint32_t*)reg.val_data;
}

/**
 * @brief compile a unbounded pipe chain
 * @note the unbounded means that it do have neither the first node nor the last node <br/>
 *       for example, "pipe1" -&gt; "pipe2" node1 "pipe3" -&gt; "pipe4" <br/> is a unbounded pipe
 * @param compiler the compiler instance
 * @param context the service graph context
 * @param result the result buffer
 * @param left_node the node that is in the left side of this chain
 * @param allow_empty indicates if we allow this chain to be empty
 * @return status code
 **/
static inline int _service_graph_unbounded_chain(lang_compiler_t* compiler, _service_graph_context_t* context,
                                                 _service_graph_pending_list_t* result, uint32_t left_node,
                                                 int allow_empty)
{
	_service_graph_pending_edge_t temp;
	_service_graph_pending_list_t childres = {NULL, NULL};
	int empty = !allow_empty;
	for(;;)
	{
		const lang_lex_token_t* token0 = _peek(compiler, 0);
		const lang_lex_token_t* token1 = _peek(compiler, 1);
		const lang_lex_token_t* token2 = _peek(compiler, 2);
		uint32_t right_node = _UNDETERMINED;

		if(LANG_LEX_TOKEN_STRING == token0->type && LANG_LEX_TOKEN_ARROW == token1->type && LANG_LEX_TOKEN_STRING == token2->type)
		{
			/* if the first token is string means the first edge is "pipe" -> "pipe" */
			uint32_t source_pipe = lang_bytecode_table_acquire_string_id(compiler->bc_tab, token0->value.s);
			if(ERROR_CODE(uint32_t) == source_pipe) _INTERNAL_ERROR_LOG("Cannot acquire the string id from the bytecode table", goto ERR);

			uint32_t dest_pipe = lang_bytecode_table_acquire_string_id(compiler->bc_tab, token2->value.s);
			if(ERROR_CODE(uint32_t) == dest_pipe) _INTERNAL_ERROR_LOG("Cannot acquire the string id from the bytecode table", goto ERR);
			_consume(compiler, 3);

			temp.type = _N2N;
			temp.source_node = left_node;
			temp.source_pipe = source_pipe;
			temp.destination_pipe = dest_pipe;
			temp.destination_node = _UNDETERMINED;
			temp.level = context->level;
			temp.next = NULL;
			temp.stack = 1;
			childres.head = &temp;
			childres.tail = &temp;
			empty = 0;
		}
		else if(LANG_LEX_TOKEN_LBRACE == token0->type)
		{
			childres.head = childres.tail = NULL;
			if(ERROR_CODE(int) == _service_graph_pipe_block(compiler, context, &childres, left_node))
			    _COMPILE_ERROR("Invalid pipe description block", goto ERR);
			empty = 0;
		}
		else break;

		token0 = _peek(compiler, 0);
		if(LANG_LEX_TOKEN_IDENTIFIER == token0->type && (ERROR_CODE(uint32_t) == (right_node = _service_graph_find_node(compiler, context))))
		    _COMPILE_ERROR("Cannot find servlet node", goto ERR);

		if(ERROR_CODE(int) == _process_pending_list(compiler, context, &childres, left_node, right_node))
		    _INTERNAL_ERROR_LOG("Cannot process the pending list", goto ERR);

		_pending_list_merge(result, &childres);

		context->right_node = right_node;

		left_node = right_node;

		if(left_node == _UNDETERMINED) break;
	}

	if(empty)
	    _COMPILE_ERROR("Empty pipe statement is not allowed", goto ERR);

	return 0;
ERR:
	_pending_list_free(result);
	_pending_list_free(&childres);
	return ERROR_CODE(int);
}

/**
 * @brief compile the service graph input pipe ()-> ....
 * @param compiler the compiler
 * @param context the graph context
 * @param result the pending list result
 * @param buf the pending edges
 * @return status code
 **/
static inline int _service_graph_pipe_input(lang_compiler_t* compiler, _service_graph_context_t* context,
                                            _service_graph_pending_list_t* result, _service_graph_pending_edge_t* buf)
{
	_consume(compiler, 1);
	/* check we have ()-> */
	if(LANG_LEX_TOKEN_RPARENTHESIS != _peek(compiler, 0)->type || LANG_LEX_TOKEN_ARROW != _peek(compiler, 1)->type)
	    _COMPILE_ERROR("`() ->' is expected", return ERROR_CODE(int));
	_consume(compiler, 2);

	const lang_lex_token_t* token = _peek(compiler, 0);

	if(LANG_LEX_TOKEN_STRING != token->type)
	    _COMPILE_ERROR("Pipe name is expected", return ERROR_CODE(int));

	uint32_t pipe = ERROR_CODE(uint32_t);
	if(ERROR_CODE(uint32_t) == (pipe = lang_bytecode_table_acquire_string_id(compiler->bc_tab, token->value.s)))
	    _INTERNAL_ERROR_LOG("Cannot acquire the string id from the bytecode table", return ERROR_CODE(int));

	_consume(compiler, 1);
	buf->type = _INPUT;
	buf->destination_node = _UNDETERMINED;
	buf->destination_pipe = pipe;
	buf->stack = 1;
	buf->level = context->level;
	buf->next = NULL;
	result->head = result->tail = buf;
	return 0;
}

/**
 * @brief compile the service graph output pipe ->() ....
 * @param compiler the compiler
 * @param context the graph context
 * @param result the pending list result
 * @param buf the pending edges
 * @param left_node the left node for this chain
 * @return status code
 **/
static inline int _service_graph_pipe_output(lang_compiler_t* compiler, _service_graph_context_t* context,
                                             _service_graph_pending_list_t* result, _service_graph_pending_edge_t* buf, uint32_t left_node)
{
	(void) context;
	const lang_lex_token_t* pipe_token = _peek(compiler, 0);
	uint32_t pipe = ERROR_CODE(uint32_t);
	if(ERROR_CODE(uint32_t) == (pipe = lang_bytecode_table_acquire_string_id(compiler->bc_tab, pipe_token->value.s)))
	    _INTERNAL_ERROR_LOG("Cannot acquire the string id from the bytecode table", return ERROR_CODE(int));

	/* we assume that the caller has alread examined it must be a output pipe */
	_consume(compiler, 3);

	if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_RPARENTHESIS)
	    _consume(compiler, 1);
	else
	    _COMPILE_ERROR("`)' expected in the output pipe desc", return ERROR_CODE(int));

	buf->type = _OUTPUT;
	buf->source_node = left_node;
	buf->source_pipe = pipe;
	buf->stack = 1;
	buf->level = context->level;
	buf->next = NULL;
	result->head = result->tail = buf;

	return 0;
}

/**
 * @brief parse a pipe statement
 * @param compiler the compiler instance
 * @param context current service graph context
 * @param result the result of pending lists
 * @param left_node the left most node passed from the upper level
 * @return status code
 **/
static inline int _service_graph_pipe_statement(lang_compiler_t* compiler, _service_graph_context_t* context,
                                                _service_graph_pending_list_t* result, uint32_t left_node)
{
	int empty = 1;
	const lang_lex_token_t* token = _peek(compiler, 0);

	_service_graph_pending_list_t input_list = {NULL, NULL};
	_service_graph_pending_edge_t input_edge;

	if(LANG_LEX_TOKEN_LPARENTHESIS == token->type)
	{
		empty = 0;
		/* If there's an input pipe, we should parse the input pipe */
		if(ERROR_CODE(int) == _service_graph_pipe_input(compiler, context, &input_list, &input_edge))
		    _COMPILE_ERROR("Cannot parse the input pipe", return ERROR_CODE(int));

		_pending_list_merge(result, &input_list);
		/* Because to token has been alread consumed by _service_graph_pipe_input, so update the token */
		token = _peek(compiler, 0);

		if(LANG_LEX_TOKEN_IDENTIFIER != token->type)  /* if this is only a () -> "pipe" */
		{
			if(ERROR_CODE(int) == _process_pending_list(compiler, context, result, left_node, _UNDETERMINED))
			    _INTERNAL_ERROR_LOG("Cannot process the pending list", return ERROR_CODE(int));
			return 0;
		}
	}

	if(LANG_LEX_TOKEN_IDENTIFIER == token->type && ERROR_CODE(uint32_t) == (left_node = _service_graph_find_node(compiler, context)))
	        _COMPILE_ERROR("Cannot find the servlet node", return ERROR_CODE(int));

	if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_STRING &&
	   _peek(compiler, 1)->type == LANG_LEX_TOKEN_ARROW  &&
	   _peek(compiler, 2)->type == LANG_LEX_TOKEN_LPARENTHESIS)
	{
		/* if this statement is only a "pipe" -> () */
		_service_graph_pending_list_t output_list = {NULL, NULL};
		_service_graph_pending_edge_t output_edge;
		if(_service_graph_pipe_output(compiler, context, &output_list, &output_edge, left_node) == ERROR_CODE(int))
		    _COMPILE_ERROR("Invalid output pipe desc", return ERROR_CODE(int));

		_pending_list_merge(result, &output_list);

		if(ERROR_CODE(int) == _process_pending_list(compiler, context, result, left_node, _UNDETERMINED))
		    _INTERNAL_ERROR_LOG("Cannot process the pending list", return ERROR_CODE(int));
		return 0;
	}

	context->right_node = _UNDETERMINED;

	/* this statement do not have a left node, if parser previously has shifted, we can allow a empty chain */
	if(ERROR_CODE(int) == _service_graph_unbounded_chain(compiler, context, result, left_node, !empty))
	    _COMPILE_ERROR("Invalid pipe chain", return ERROR_CODE(int));

	token = _peek(compiler, 0);
	uint32_t right_node = context->right_node;

	if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_STRING &&
	   _peek(compiler, 1)->type == LANG_LEX_TOKEN_ARROW  &&
	   _peek(compiler, 2)->type == LANG_LEX_TOKEN_LPARENTHESIS)
	{
		/* Basically, it's impossible that the pipe definition like
		 *     () -> "xxx" node "yyy" -> ()
		 * can fell to this, because we take this special case before processing the chain.
		 * And this is only valid form that a chain be empty
		 * So if the chain is empty, we won't be here. So the last position of the parser will remains
		 * at () -> "xxxx", which means Ok. And what we actually want to do here is to prevent
		 * parser accept a 0 length statement, which causes the parser fell in to a infinite loop */
		if(right_node == _UNDETERMINED)
		    _COMPILE_ERROR("Unspecified output node", return ERROR_CODE(int));

		/* process the output pipe */
		_service_graph_pending_list_t output_list = {NULL, NULL};
		_service_graph_pending_edge_t output_edge;
		if(_service_graph_pipe_output(compiler, context, &output_list, &output_edge, right_node) == ERROR_CODE(int))
		    _COMPILE_ERROR("Invalid output pipe desc", return ERROR_CODE(int));
		_pending_list_merge(result, &output_list);
	}

	if(ERROR_CODE(int) == _process_pending_list(compiler, context, result, left_node, right_node))
	    _INTERNAL_ERROR_LOG("Cannot process the pending list", return ERROR_CODE(int));

	return 0;
}

/**
 * @brief parse a pipe block statement
 * @param compiler the compiler instance
 * @param context the context of the service graph
 * @param result the result pending edges
 * @param left_node the left most node passed from the upper level
 * @return status code
 **/
static inline int _service_graph_pipe_block(lang_compiler_t* compiler, _service_graph_context_t* context,
                                            _service_graph_pending_list_t* result, uint32_t left_node)
{
	context->level ++;
	_consume(compiler, 1);   /* consume the `{' */
	for(;;)
	{
		const lang_lex_token_t* start = _peek(compiler, 0);
		_service_graph_pending_list_t child = {NULL, NULL};

		if(LANG_LEX_TOKEN_RBRACE == start->type)
		{
			_consume(compiler, 1);
			break;
		}
		if(LANG_LEX_TOKEN_IDENTIFIER != start->type &&
		   LANG_LEX_TOKEN_LPARENTHESIS != start->type &&
		   LANG_LEX_TOKEN_LBRACE != start->type &&
		   LANG_LEX_TOKEN_STRING != start->type)
		    _COMPILE_ERROR("Pipe statement expected", return ERROR_CODE(int));

		if(ERROR_CODE(int) == _service_graph_pipe_statement(compiler, context, &child, left_node))
		    _COMPILE_ERROR("Invalid pipe statement", return ERROR_CODE(int));

		_pending_list_merge(result, &child);

		if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_SEMICOLON)
		    _consume(compiler, 1);
	}

	context->level --;
	return 0;
}

/**
 * @brief parse a service node declaration
 * @param compiler the compiler context
 * @param context the service grpah context
 * @return status code
 **/
static inline int _service_node(lang_compiler_t* compiler, _service_graph_context_t* context)
{
	uint32_t reg = ERROR_CODE(uint32_t);
	_REGALLOC(reg, _INTERNAL_ERROR(return ERROR_CODE(int)));

	const lang_lex_token_t* id = _peek(compiler, 0);
	uint32_t name  = ERROR_CODE(uint32_t);
	uint32_t param = ERROR_CODE(uint32_t);
	uint32_t g_reg = ERROR_CODE(uint32_t);
	const lang_lex_token_t* graphviz = NULL;
	vector_t* new_regs;

	hashmap_find_res_t result;
	switch(hashmap_insert(context->node_map, id->value.s, strlen(id->value.s) + 1, &reg, sizeof(uint32_t), &result, 0))
	{
		case ERROR_CODE(int):
		    _INTERNAL_ERROR_LOG("Cannot insert the new node -> register map to hashmap", break);
		case 0:
		    _COMPILE_ERROR("Redefinition of service graph node is not allowed in the same service graph node", break);
		    break;
		case 1:
		    LOG_DEBUG("Service node `%s' has been mapped to register R%u", id->value.s, reg);
		    goto PARSE_NODE_PARAM;
	}
	goto ERR;

PARSE_NODE_PARAM:
	/* push graph register */
	_INS_1OP(pusharg, _OP(REG, reg, context->graph_reg), goto ERR);

	/* push the servlet name */
	_REGALLOC(name, goto ERR);
	_INS_2OP(move, _OP(REG, reg, name), _OP(STR, str, id->value.s), goto ERR);
	_INS_1OP(pusharg, _OP(REG, reg, name), goto ERR);

	_consume(compiler, 2);

	/* push servlet param */
	param = _rvalue(compiler);
	if(ERROR_CODE(uint32_t) == param)
	    _COMPILE_ERROR("Invalid servlet parameter", goto ERR);

	_INS_1OP(pusharg, _OP(REG, reg, param), goto ERR);

	/* push graphviz */
	graphviz = _peek(compiler, 0);
	if(graphviz->type == LANG_LEX_TOKEN_GRAPHVIZ_PROP)
	{
		_REGALLOC(g_reg, goto ERR);

		_INS_2OP(move, _OP(REG, reg, g_reg), _OP(GRAPHVIZ, graphviz, graphviz->value.s), goto ERR);
		_INS_1OP(pusharg, _OP(REG, reg, g_reg), goto ERR);

		_consume(compiler, 1);
	}

	/* invoke */
	_INS_2OP(invoke, _OP(REG, reg, reg), _OP(BUILTIN, builtin, LANG_BYTECODE_BUILTIN_ADD_NODE), goto ERR);
	_REGFREE(g_reg, goto ERR);
	_REGFREE(param, goto ERR);
	_REGFREE(name, goto ERR);

	new_regs = vector_append(context->regs, &reg);
	if(NULL == new_regs)
	    _INTERNAL_ERROR_LOG("Cannot append the register to the using register list", goto ERR);
	context->regs = new_regs;

	return 0;
ERR:
	_REGFREE(reg, /* NOP */);
	_REGFREE(param, /* NOP */);
	_REGFREE(g_reg, /* NOP */);
	_REGFREE(name, /* NOP */);
	return ERROR_CODE(int);
}

/**
 * @brief parse a service graph literal and store the result to the given register
 * @param compiler the compiler context
 * @param reg the target register
 **/
static inline int _service_graph(lang_compiler_t* compiler, uint32_t reg)
{
	int success = 0;

	_consume(compiler, 1);

	_service_graph_context_t context = {
		.node_map = NULL,
		.regs = NULL,
		.graph_reg = reg,
		.level = 0
	};

	if(NULL == (context.node_map = hashmap_new(LANG_COMPILER_NODE_HASH_SIZE, LANG_COMPILER_NODE_HASH_POOL_INIT_SIZE)))
	    _INTERNAL_ERROR_LOG("Cannot allocate node map for the service graph context" , goto ERR);

	if(NULL == (context.regs = vector_new(sizeof(uint32_t), LANG_COMPILER_NODE_HASH_SIZE)))
	    _INTERNAL_ERROR_LOG("Cannot create register list", goto ERR);

	_INS_2OP(invoke, _OP(REG, reg, reg), _OP(BUILTIN, builtin, LANG_BYTECODE_BUILTIN_NEW_GRAPH), goto ERR);

	for(;;)
	{
		const lang_lex_token_t* next1 = _peek(compiler, 0);
		const lang_lex_token_t* next2 = _peek(compiler, 1);

		if(next1->type == LANG_LEX_TOKEN_IDENTIFIER && next2->type == LANG_LEX_TOKEN_COLON_EQUAL)
		{
			if(ERROR_CODE(int) == _service_node(compiler, &context)) _COMPILE_ERROR("Invalid service node definition", goto ERR);
		}
		else if(next1->type == LANG_LEX_TOKEN_RBRACE)
		{
			_consume(compiler, 1);
			break;
		}
		else if(next1->type == LANG_LEX_TOKEN_IDENTIFIER ||     /* node "pipe" -> .... */
		        next1->type == LANG_LEX_TOKEN_LPARENTHESIS ||   /* () -> .... */
		        next1->type == LANG_LEX_TOKEN_LBRACE)           /* { ... } node */
		{
			_service_graph_pending_list_t list = {NULL, NULL};

			if(_service_graph_pipe_statement(compiler, &context, &list, _UNDETERMINED) == ERROR_CODE(int))
			    _COMPILE_ERROR("Error found during parsing the pipe chain", goto ERR);
			else if(NULL != list.head)
			{
				_pending_list_free(&list);
				_COMPILE_ERROR("Invalid pending edge in the top scope, a servlet node missing?", goto ERR);
			}
		}
		else
		    _COMPILE_ERROR("Unexpected token in service graph literal", goto ERR);

		if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_SEMICOLON)
		    _consume(compiler, 1);
	}
	success = 1;
ERR:
	if(context.regs != NULL)
	{
		size_t i;
		for(i = 0; i < vector_length(context.regs); i ++)
		{
			uint32_t reg = *VECTOR_GET_CONST(uint32_t, context.regs, i);
			_REGFREE(reg, /* NOP */);
		}
		vector_free(context.regs);
	}
	if(context.node_map != NULL) hashmap_free(context.node_map);

	if(success) return 0;

	_REGFREE(reg, /* NOP */);

	ERROR_RETURN(int);
}

/**
 * @brief parse the r-value primitive
 * @param compiler the compiler instance
 * @return the register that holds this rvalue
 **/
static inline uint32_t _rvalue_primitive(lang_compiler_t* compiler)
{
	const lang_lex_token_t* next = _peek(compiler, 0);
	uint32_t reg = ERROR_CODE(uint32_t);

	if(LANG_LEX_TOKEN_LPARENTHESIS == next->type)
	{
		/* This is a parenthesis "(....)" */

		_consume(compiler, 1);
		if(ERROR_CODE(uint32_t) == (reg = _rvalue(compiler)))
		    _COMPILE_ERROR("Invalid r-value expression", return ERROR_CODE(uint32_t));

		_EXPECT_TOKEN(0, RPARENTHESIS,
		    _REGFREE(reg, /* NOP */);
		    return ERROR_CODE(uint32_t);
		);

		_consume(compiler, 1);

		return reg;
	}

	_REGALLOC(reg, _INTERNAL_ERROR(return ERROR_CODE(uint32_t)));

	lang_bytecode_operand_t src;
	uint32_t* sym = NULL;

	if(LANG_LEX_TOKEN_MINUS == next->type)
	{
		/* - primitive */
		src.type = LANG_BYTECODE_OPERAND_INT;
		src.num  = 0;
		_consume(compiler, 1);
		uint32_t inner_reg = _rvalue_primitive(compiler);
		_INS_ARITHMETIC(LANG_BYTECODE_OPCODE_SUB, _OP(REG, reg, reg), src, _OP(REG, reg, inner_reg), _INTERNAL_ERROR(
		    _REGFREE(reg, /* NOP */);
		    _REGFREE(inner_reg, /* NOP */);
		    return ERROR_CODE(uint32_t);
		));
		_REGFREE(inner_reg, /* NOP */);
		return reg;
	}
	else if(LANG_LEX_TOKEN_NOT == next->type)
	{
		/* !a -> 0!=a */
		src.type = LANG_BYTECODE_OPERAND_INT;
		src.num = 0;
		_consume(compiler, 1);
		uint32_t inner_reg = _rvalue_primitive(compiler);
		_INS_ARITHMETIC(LANG_BYTECODE_OPCODE_EQ, _OP(REG, reg, reg), src, _OP(REG, reg, inner_reg), _INTERNAL_ERROR(
		    _REGFREE(reg, /* NOP */);
		    _REGFREE(inner_reg, /* NOP */);
		    return ERROR_CODE(uint32_t);
		));
		_REGFREE(inner_reg, /* NOP */);
		return reg;
	}
	else if(LANG_LEX_TOKEN_INTEGER == next->type)
	{
		src.type = LANG_BYTECODE_OPERAND_INT;
		src.num = next->value.i;
		_consume(compiler, 1);
	}
	else if(LANG_LEX_TOKEN_STRING == next->type)
	{
		src.type = LANG_BYTECODE_OPERAND_STR;
		src.str = next->value.s;
		_consume(compiler, 1);
	}
	else if(LANG_LEX_TOKEN_IDENTIFIER == next->type)
	{
		_PARSE_SYM(sym, "r-value", 1, return ERROR_CODE(uint32_t));
		src.type = LANG_BYTECODE_OPERAND_SYM;
		src.sym = sym;
	}
	else if(LANG_LEX_TOKEN_LBRACE == next->type)
	{
		if(_service_graph(compiler, reg) == ERROR_CODE(int))
		    _COMPILE_ERROR("Invalid service graph in r-value", return ERROR_CODE(uint32_t));
		return reg;
	}
	else if(LANG_LEX_TOKEN_KEYWORD == next->type && LANG_LEX_KEYWORD_UNDEFINED == next->value.k)
	{
		_consume(compiler, 1);
		_INS_1OP(undefined, _OP(REG, reg, reg), _INTERNAL_ERROR(
		    _REGFREE(reg, /* NOP */);
		    return ERROR_CODE(uint32_t);
		));

		return reg;
	}
	else
	    _COMPILE_ERROR("R-value expected", return ERROR_CODE(uint32_t));

	_INS_2OP(move, _OP(REG, reg, reg), src, _INTERNAL_ERROR(
	    _REGFREE(reg, /* NOP */);
	    return ERROR_CODE(uint32_t)
	));

	LOG_DEBUG("R-value has been assigned to register %u", reg);

	if(NULL != sym) free(sym);

	return reg;
}

/**
 * @brief parse the r-value
 * @param compiler the compiler instance
 * @return the register that holds this value
 **/
static inline uint32_t _rvalue(lang_compiler_t* compiler)
{
	const lang_lex_token_t* next = _peek(compiler, 0);

	uint32_t          reg_stack[128];
	lang_lex_token_type_t  tok_stack[128];
	uint32_t sp = 0;

	static const int _p[LANG_LEX_TOKEN_NUM_OF_ENTRIES] = {
		[LANG_LEX_TOKEN_AND]           = 1,
		[LANG_LEX_TOKEN_OR]            = 1,
		[LANG_LEX_TOKEN_EQUALEQUAL]    = 2,
		[LANG_LEX_TOKEN_NE]            = 2,
		[LANG_LEX_TOKEN_LT]            = 2,
		[LANG_LEX_TOKEN_LE]            = 2,
		[LANG_LEX_TOKEN_GT]            = 2,
		[LANG_LEX_TOKEN_GE]            = 2,
		[LANG_LEX_TOKEN_ADD]           = 3,
		[LANG_LEX_TOKEN_MINUS]         = 3,
		[LANG_LEX_TOKEN_DIVIDE]        = 4,
		[LANG_LEX_TOKEN_TIMES]         = 4,
		[LANG_LEX_TOKEN_MODULAR]       = 4
	};

	for(;;)
	{
		reg_stack[sp] = _rvalue_primitive(compiler);
		if(ERROR_CODE(uint32_t) == reg_stack[sp]) goto ERR;

		next = _peek(compiler, 0);

		int p = _p[next->type];

		for(;sp > 0 && _p[tok_stack[sp - 1]] >= p; sp--)
		{
			uint32_t new_reg = ERROR_CODE(uint32_t);
			_REGALLOC(new_reg,
			    _REGFREE(reg_stack[sp], /* NOP */);
			    _INTERNAL_ERROR(goto ERR);
			);

			lang_bytecode_opcode_t opcode;
			switch(tok_stack[sp - 1])
			{
				case LANG_LEX_TOKEN_AND:
				    opcode = LANG_BYTECODE_OPCODE_AND;
				    break;
				case LANG_LEX_TOKEN_OR:
				    opcode = LANG_BYTECODE_OPCODE_OR;
				    break;
				case LANG_LEX_TOKEN_EQUALEQUAL:
				    opcode = LANG_BYTECODE_OPCODE_EQ;
				    break;
				case LANG_LEX_TOKEN_NE:
				    opcode = LANG_BYTECODE_OPCODE_NE;
				    break;
				case LANG_LEX_TOKEN_LT:
				    opcode = LANG_BYTECODE_OPCODE_LT;
				    break;
				case LANG_LEX_TOKEN_LE:
				    opcode = LANG_BYTECODE_OPCODE_LE;
				    break;
				case LANG_LEX_TOKEN_GT:
				    opcode = LANG_BYTECODE_OPCODE_GT;
				    break;
				case LANG_LEX_TOKEN_GE:
				    opcode = LANG_BYTECODE_OPCODE_GE;
				    break;
				case LANG_LEX_TOKEN_ADD:
				    opcode = LANG_BYTECODE_OPCODE_ADD;
				    break;
				case LANG_LEX_TOKEN_MINUS:
				    opcode = LANG_BYTECODE_OPCODE_SUB;
				    break;
				case LANG_LEX_TOKEN_TIMES:
				    opcode = LANG_BYTECODE_OPCODE_MUL;
				    break;
				case LANG_LEX_TOKEN_DIVIDE:
				    opcode = LANG_BYTECODE_OPCODE_DIV;
				    break;
				case LANG_LEX_TOKEN_MODULAR:
				    opcode = LANG_BYTECODE_OPCODE_MOD;
				    break;
				default:
				    _COMPILE_ERROR("Invalid arithmetic operator",
				        _REGFREE(reg_stack[sp], /* NOP */);
				        _REGFREE(new_reg, /* NOP */);
				        goto ERR;
				    );
			}

			_INS_ARITHMETIC(opcode, _OP(REG, reg, new_reg), _OP(REG, reg, reg_stack[sp-1]), _OP(REG, reg, reg_stack[sp]),
			    _REGFREE(reg_stack[sp], /* NOP */);
			    _REGFREE(new_reg, /* NOP */);
			    goto ERR;
			);

			_REGFREE(reg_stack[sp - 1], /* NOP */);
			_REGFREE(reg_stack[sp], /* NOP */);

			reg_stack[sp - 1] = new_reg;
		}

		if(_p[next->type] == 0) break;

		tok_stack[sp ++] = next->type;
		_consume(compiler, 1);
	}

	return reg_stack[0];
ERR:
	for(;sp > 0; sp--)
	    _REGFREE(reg_stack[sp], /* NOP */);
	return ERROR_CODE(uint32_t);
}

/**
 * @brief parse the assignment statement with the lhs already parsed
 * @param compiler the compiler context
 * @param lhs the LHS symbol
 * @param decl decl mode
 * @return status code
 **/
static inline int _assignment_rhs(lang_compiler_t* compiler, uint32_t* lhs, int decl)
{
	int rc = ERROR_CODE(int);
	uint32_t reg = ERROR_CODE(uint32_t);

	/* if this statement is a declaration, so we need to add the new symbol to the var list */
	if(decl && ERROR_CODE(int) == _define_symbol(compiler, lhs))
	    _INTERNAL_ERROR(goto CLEANUP);

	lang_bytecode_opcode_t opcode = 0;
	lang_lex_token_type_t toktype = _peek(compiler, 0)->type;
	switch(toktype) {
		case LANG_LEX_TOKEN_ADD_EQUAL:
		    opcode = LANG_BYTECODE_OPCODE_ADD;
		    break;
		case LANG_LEX_TOKEN_MINUS_EQUAL:
		    opcode = LANG_BYTECODE_OPCODE_SUB;
		    break;
		case LANG_LEX_TOKEN_MODULAR_EQUAL:
		    opcode = LANG_BYTECODE_OPCODE_MOD;
		    break;
		case LANG_LEX_TOKEN_TIMES_EQUAL:
		    opcode = LANG_BYTECODE_OPCODE_MUL;
		    break;
		case LANG_LEX_TOKEN_DIVIDE_EQUAL:
		    opcode = LANG_BYTECODE_OPCODE_DIV;
		    break;
		case LANG_LEX_TOKEN_EQUAL:
		    break;
		default:
		    goto CLEANUP;
	}

	_consume(compiler, 1);

	if((reg = _rvalue(compiler)) == ERROR_CODE(uint32_t))
	    _COMPILE_ERROR("Invalid r-value in an assignment statement", goto CLEANUP);

	if(toktype == LANG_LEX_TOKEN_EQUAL)
	    _INS_2OP(move, _OP(SYM, sym, lhs), _OP(REG, reg, reg), _INTERNAL_ERROR(goto CLEANUP));
	else
	    _INS_ARITHMETIC(opcode, _OP(SYM, sym, lhs), _OP(SYM, sym, lhs), _OP(REG, reg, reg), _INTERNAL_ERROR(goto CLEANUP));

	rc = 0;
CLEANUP:
	_REGFREE(reg, _INTERNAL_ERROR(rc = ERROR_CODE(int)));
	return rc;
}

/**
 * @brief parse the assignment statement
 * @param compiler the compiler context
 * @return status code
 **/
static inline int _assignment(lang_compiler_t* compiler)
{
	int rc = 0;
	uint32_t* sym;
	while(rc != ERROR_CODE(int))
	{
		_PARSE_SYM(sym, "Assignment statement", 1, return ERROR_CODE(int));
		rc = _assignment_rhs(compiler, sym, 0);
		if(NULL != sym) free(sym);
		if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_COMMA) break;
		_consume(compiler, 1);
	}
	return rc;
}

static inline int _builtin_invoke_1arg(lang_compiler_t* compiler, lang_bytecode_builtin_t func)
{
	_consume(compiler, 1);
	uint32_t reg = _rvalue(compiler);
	uint32_t target_reg = ERROR_CODE(uint32_t);

	if(ERROR_CODE(uint32_t) == reg) _COMPILE_ERROR("Invalid r-value", return ERROR_CODE(int));

	_INS_1OP(pusharg, _OP(REG, reg, reg), _INTERNAL_ERROR(goto ERR));

	_REGALLOC(target_reg, _INTERNAL_ERROR(goto ERR));
	_INS_2OP(invoke, _OP(REG, reg, target_reg), _OP(BUILTIN, builtin, func), _INTERNAL_ERROR(goto ERR));

	_REGFREE(target_reg, _INTERNAL_ERROR(goto ERR));
	_REGFREE(reg, _INTERNAL_ERROR(goto ERR));

	return 0;
ERR:
	_REGFREE(reg, /* NOP */);
	_REGFREE(target_reg, /* NOP */);
	return ERROR_CODE(int);
}
static inline int _echo_statement(lang_compiler_t* compiler)
{
	return _builtin_invoke_1arg(compiler, LANG_BYTECODE_BUILTIN_ECHO);
}

static inline int _insmod_statement(lang_compiler_t* compiler)
{
	return _builtin_invoke_1arg(compiler, LANG_BYTECODE_BUILTIN_INSMOD);
}

static inline int _if_statement(lang_compiler_t* compiler)
{
	int rc = ERROR_CODE(int);
	_consume(compiler, 1);

	uint32_t l_else = _new_label(compiler);
	uint32_t l_end = _new_label(compiler);
	if(ERROR_CODE(uint32_t) == l_end || ERROR_CODE(uint32_t) == l_else)
	    _INTERNAL_ERROR(return ERROR_CODE(int));

	// condition
	_EXPECT_TOKEN(0, LPARENTHESIS, _COMPILE_ERROR("Expect (", goto RET));
	_consume(compiler, 1);
	uint32_t cond_res = _rvalue(compiler);
	if(ERROR_CODE(uint32_t) == cond_res)
	    _COMPILE_ERROR("Can not parse the condition of if", return ERROR_CODE(int));
	_EXPECT_TOKEN(0, RPARENTHESIS, _COMPILE_ERROR("Expect )", goto RET));
	_consume(compiler, 1);

	_INS_2OP(jz, _OP(REG, reg, cond_res), _OP(LABEL, label, l_else), _INTERNAL_ERROR(goto RET));

	if(_statement(compiler) != 1)
	    _COMPILE_ERROR("Invalid statement", goto RET);

	_INS_1OP(jump, _OP(LABEL, label, l_end), _INTERNAL_ERROR(goto RET));

	// else
	if(ERROR_CODE(int) == _set_label_target(compiler, l_else))
	    _INTERNAL_ERROR(return ERROR_CODE(int));

	if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_KEYWORD)
	{
		if(_peek(compiler, 0)->value.k == LANG_LEX_KEYWORD_ELSE)
		{
			_consume(compiler, 1);
			if(_statement(compiler) != 1)
			    _COMPILE_ERROR("Invalid statement", goto RET);
		}
	}
	// endif
	if(ERROR_CODE(int) == _set_label_target(compiler, l_end))
	    _INTERNAL_ERROR(return ERROR_CODE(int));
	rc = 0;
RET:
	return rc;
}

static inline int _visualize_statement(lang_compiler_t* compiler)
{
	_consume(compiler, 1);
	uint32_t reg = _rvalue(compiler);
	uint32_t target_reg = ERROR_CODE(uint32_t);
	uint32_t file_reg = ERROR_CODE(uint32_t);

	if(ERROR_CODE(uint32_t) == reg) _COMPILE_ERROR("Invalid service r-value", return ERROR_CODE(int));
	if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_TRIPLE_GT) _COMPILE_ERROR("`>>>' is expected in a visualize statement", goto ERR);
	_consume(compiler, 1);
	file_reg = _rvalue(compiler);
	if(ERROR_CODE(uint32_t) == reg) _COMPILE_ERROR("Invalid target file r-value", goto ERR);
	_INS_1OP(pusharg, _OP(REG, reg, reg), _INTERNAL_ERROR(goto ERR));
	_INS_1OP(pusharg, _OP(REG, reg, file_reg), _INTERNAL_ERROR(goto ERR));
	_REGALLOC(target_reg, _INTERNAL_ERROR(goto ERR));
	_INS_2OP(invoke, _OP(REG, reg, target_reg), _OP(BUILTIN, builtin, LANG_BYTECODE_BUILTIN_GRAPHVIZ), _INTERNAL_ERROR(goto ERR));

	_REGALLOC(target_reg, _INTERNAL_ERROR(goto ERR));
	_REGALLOC(file_reg, _INTERNAL_ERROR(goto ERR));
	_REGALLOC(reg, _INTERNAL_ERROR(goto ERR));
	return 0;
ERR:
	_REGFREE(reg, /* NOP */);
	_REGFREE(target_reg, /* NOP */);
	_REGFREE(file_reg, /* NOP */);
	return ERROR_CODE(int);
}

static inline int _start_statement(lang_compiler_t* compiler)
{
	return _builtin_invoke_1arg(compiler, LANG_BYTECODE_BUILTIN_START);
}

static inline int _var_statement(lang_compiler_t* compiler)
{
	_consume(compiler, 1);

	for(;;)
	{
		uint32_t* sym;

		_PARSE_SYM(sym, "var statement", 0, return ERROR_CODE(int));

		if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_EQUAL)
		{
			/* this is simply a decl */
			_define_symbol(compiler, sym);
		}
		else if(_assignment_rhs(compiler, sym, 1) == ERROR_CODE(int))
		    _COMPILE_ERROR("Invalid var assignment", goto ERR);

		free(sym);

		if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_COMMA)
		    return 0;

		_consume(compiler, 1);
		continue;
ERR:
		free(sym);
		return ERROR_CODE(int);
	}
}

static inline int _while_statement(lang_compiler_t* compiler)
{
	_consume(compiler, 1);

	/* we don't need to actualy consume this, becuase even with the wrapper, it's still valid */
	_EXPECT_TOKEN(0, LPARENTHESIS, _COMPILE_ERROR("`(' expected", return ERROR_CODE(int)));

	uint32_t l_cond = _new_label(compiler);
	uint32_t l_end  = _new_label(compiler);
	if(ERROR_CODE(uint32_t) == l_cond || ERROR_CODE(uint32_t) == l_end)
	    _INTERNAL_ERROR(return ERROR_CODE(int));

	if(ERROR_CODE(int) == _set_label_target(compiler, l_cond))
	    _INTERNAL_ERROR(return ERROR_CODE(int));

	uint32_t cond_res = _rvalue(compiler);

	if(ERROR_CODE(uint32_t) == cond_res)
	    _COMPILE_ERROR("Cannot parse the condition in while loop", return ERROR_CODE(int));

	/* insert the jump instruction after the cond res */

	_INS_2OP(jz, _OP(REG, reg, cond_res), _OP(LABEL, label, l_end), _INTERNAL_ERROR(goto ERR));

	_REGFREE(cond_res, return ERROR_CODE(int));
	cond_res = ERROR_CODE(uint32_t);

	compiler->cont_label = l_cond;
	compiler->brk_label = l_end;

	/* now let's parse the loop body */
	if(ERROR_CODE(int) == _statement(compiler))
	    _COMPILE_ERROR("Cannot parse loop body", _INTERNAL_ERROR(goto ERR));

	_INS_1OP(jump, _OP(LABEL, label, l_cond), _INTERNAL_ERROR(goto ERR));

	if(ERROR_CODE(int) == _set_label_target(compiler, l_end))
	    _INTERNAL_ERROR(return ERROR_CODE(int));

	return 0;
ERR:
	_REGFREE(cond_res, /* NOP */);
	return ERROR_CODE(int);
}

static inline int _for_statement(lang_compiler_t* compiler)
{
	_EXPECT_TOKEN(1, LPARENTHESIS, _COMPILE_ERROR("`(' expected", return ERROR_CODE(int)));
	_consume(compiler, 2);

	if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_KEYWORD &&
	   _peek(compiler, 0)->value.k == LANG_LEX_KEYWORD_VAR)
	{
		/* for(var ....; ... ; ...) */
		if(ERROR_CODE(int) == _var_statement(compiler))
		    _COMPILE_ERROR("Invalid initializer", return ERROR_CODE(int));
	}
	else if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_SEMICOLON)
	{
		if(ERROR_CODE(int) == _assignment(compiler))
		    _COMPILE_ERROR("Invalid initializer", return ERROR_CODE(int));
	}

	_EXPECT_TOKEN(0, SEMICOLON, return ERROR_CODE(int));
	_consume(compiler, 1);

	uint32_t l_cond = _new_label(compiler);
	uint32_t l_next = _new_label(compiler);
	uint32_t l_body = _new_label(compiler);
	uint32_t l_end  = _new_label(compiler);
	if(ERROR_CODE(uint32_t) == l_cond ||
	   ERROR_CODE(uint32_t) == l_end  ||
	   ERROR_CODE(uint32_t) == l_next ||
	   ERROR_CODE(uint32_t) == l_body)
	    _INTERNAL_ERROR(return ERROR_CODE(int));

	/* condition */
	uint32_t cond_reg = ERROR_CODE(uint32_t);

	if(ERROR_CODE(int) == _set_label_target(compiler, l_cond))
	    _INTERNAL_ERROR(return ERROR_CODE(int));

	if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_SEMICOLON)
	{
		if(ERROR_CODE(uint32_t) == (cond_reg = _rvalue(compiler)))
		    _COMPILE_ERROR("Invalid conditon", goto ERR);

		_INS_2OP(jz, _OP(REG, reg, cond_reg), _OP(LABEL, label, l_end), _INTERNAL_ERROR(goto ERR));
		_REGFREE(cond_reg, return ERROR_CODE(int));
		cond_reg = ERROR_CODE(uint32_t);
	}

	_INS_1OP(jump, _OP(LABEL, label, l_body), _INTERNAL_ERROR(goto ERR));

	_EXPECT_TOKEN(0, SEMICOLON, goto ERR);
	_consume(compiler, 1);

	/* next */
	if(ERROR_CODE(int) == _set_label_target(compiler, l_next))
	    _INTERNAL_ERROR(goto ERR);
	if(_peek(compiler, 0)->type != LANG_LEX_TOKEN_RPARENTHESIS)
	{
		if(ERROR_CODE(int) == _assignment(compiler))
		    _COMPILE_ERROR("Invalid next expression", goto ERR);
	}
	_INS_1OP(jump, _OP(LABEL, label, l_cond), _INTERNAL_ERROR(goto ERR));
	_EXPECT_TOKEN(0, RPARENTHESIS, goto ERR);
	_consume(compiler, 1);

	/* parse body */
	if(ERROR_CODE(int) == _set_label_target(compiler, l_body))
	    _INTERNAL_ERROR(goto ERR);

	compiler->cont_label = l_next;
	compiler->brk_label = l_end;
	if(ERROR_CODE(int) == _statement(compiler))
	    _INTERNAL_ERROR(goto ERR);

	_INS_1OP(jump, _OP(LABEL, label, l_next), _INTERNAL_ERROR(goto ERR));

	if(ERROR_CODE(int) == _set_label_target(compiler, l_end))
	    _INTERNAL_ERROR(goto ERR);

	return 0;
ERR:
	_REGFREE(cond_reg, /* NOP */);
	return ERROR_CODE(int);
}

static inline int _break_or_continue(lang_compiler_t* compiler)
{
	uint32_t label = _peek(compiler, 0)->value.k == LANG_LEX_KEYWORD_BREAK ? compiler->brk_label : compiler->cont_label;
	_consume(compiler, 1);

	if(ERROR_CODE(uint32_t) == label)
	    _COMPILE_ERROR("break/continue statement out side of loop body", return ERROR_CODE(int));

	_INS_1OP(jump, _OP(LABEL, label, label), _INTERNAL_ERROR(return ERROR_CODE(int)));

	return 0;
}

static inline int _block_statement(lang_compiler_t* compiler)
{
	int rc = ERROR_CODE(int);
	_consume(compiler, 1);

	_block_t* block = (_block_t*)malloc(sizeof(_block_t));
	if(NULL == block)
	    _INTERNAL_ERROR_LOG("Cannot allocate block", return ERROR_CODE(int));

	block->parent = compiler->current_block;
	block->block_id = compiler->next_block_id ++;

	compiler->current_block = block;

	if(_statement_list(compiler) == ERROR_CODE(int))
	    _COMPILE_ERROR("Invalid block statement", goto RET);

	_EXPECT_TOKEN(0, RBRACE, _COMPILE_ERROR("End of block excepted", goto RET));
	_consume(compiler, 1);

	rc = 0;
RET:
	if(NULL != block)
	{
		compiler->current_block = block->parent;
		free(block);
	}
	return rc;
}

/**
 * @brief parse a single statement (also with the `;' in the end of statement)
 * @param compiler the compiler context
 * @return the number of statement has been parsed, or error code
 **/
static inline int _statement(lang_compiler_t* compiler)
{
#define _RET(val) do {retval = (val); goto RET;} while(0)
	int retval = 0;
	uint32_t current_cont = compiler->cont_label;
	uint32_t current_brk  = compiler->brk_label;
	const lang_lex_token_t* token = _peek(compiler, 0);
	switch(token->type)
	{
		case LANG_LEX_TOKEN_EOF:
		    if(compiler->inc_level == 0) _RET(0);
		    else if(lang_lex_pop_include_script(compiler->lexer) == ERROR_CODE(int))
		        _INTERNAL_ERROR_LOG("Cannot pop the include script", _RET(ERROR_CODE(int)));
		    compiler->inc_level --;
		    _consume(compiler, 1);
		    break;
		case LANG_LEX_TOKEN_IDENTIFIER:
		    if(_assignment(compiler) == ERROR_CODE(int))
		        _COMPILE_ERROR("Invalid assignment", _RET(ERROR_CODE(int)));
		    break;
		case LANG_LEX_TOKEN_LBRACE:
		    if(_block_statement(compiler) == ERROR_CODE(int))
		        _COMPILE_ERROR("Invalid the block statement", _RET(ERROR_CODE(int)));
		    break;
		case LANG_LEX_TOKEN_SEMICOLON:
		    break;
		case LANG_LEX_TOKEN_KEYWORD:
		    switch(token->value.k)
		    {
			    case LANG_LEX_KEYWORD_INCLUDE:
			        {
				        _consume(compiler, 1);
				        const lang_lex_token_t* filename = _peek(compiler, 0);
				        if(NULL == filename || filename->type != LANG_LEX_TOKEN_STRING)
				            _COMPILE_ERROR("String literal expected", _RET(ERROR_CODE(int)));

				        const char* path = filename->value.s;
				        if(lang_lex_include_script(compiler->lexer, path) == ERROR_CODE(int))
				            _COMPILE_ERROR("Source code file not found", _RET(ERROR_CODE(int)));

				        LOG_DEBUG("Successfully included source code file %s", path);
				        compiler->inc_level ++;
				        _consume(compiler, 1);
			        }
			        break;
			    case LANG_LEX_KEYWORD_WHILE:
			        if(_while_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid while loop", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_FOR:
			        if(_for_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid for loop", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_BREAK:
			    case LANG_LEX_KEYWORD_CONTINUE:
			        if(_break_or_continue(compiler))
			            _COMPILE_ERROR("Invalid break/continue", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_VAR:
			        if(_var_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid var statement", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_ECHO:
			        if(_echo_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid echo statement", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_VISUALIZE:
			        if(_visualize_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid visualize statement", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_START:
			        if(_start_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid start statement", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_INSMOD:
			        if(_insmod_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid insmod statement", _RET(ERROR_CODE(int)));
			        break;
			    case LANG_LEX_KEYWORD_IF:
			        if(_if_statement(compiler) == ERROR_CODE(int))
			            _COMPILE_ERROR("Invalid if statement", _RET(ERROR_CODE(int)));
			        break;
			    default:
			        _COMPILE_ERROR("Invalid keyword token", _RET(ERROR_CODE(int)));
		    }
		    break;
		case LANG_LEX_TOKEN_RBRACE:
		    if(NULL != compiler->current_block) /* if we are currently in a block */
		        _RET(0);
		default:
		    _COMPILE_ERROR("Unexpected token", _RET(ERROR_CODE(int)));
	}

	if(_peek(compiler, 0)->type == LANG_LEX_TOKEN_SEMICOLON)
	    _consume(compiler, 1);
	retval = 1;
#undef _RET
RET:
	compiler->cont_label = current_cont;
	compiler->brk_label = current_brk;
	return retval;
}

/**
 * @brief parse a stataement list
 * @param compiler the compiler instance
 * @return the status code
 **/
static inline int _statement_list(lang_compiler_t* compiler)
{
	int rc = 0;
	while((rc = _statement(compiler)) == 1);
	return rc;
}

int lang_compiler_compile(lang_compiler_t* compiler)
{
	if(NULL == compiler) ERROR_RETURN_LOG(int, "Invalid arguments");
	int rc = _statement_list(compiler);
	if(rc != ERROR_CODE(int) && ERROR_CODE(int) == lang_bytecode_table_patch(compiler->bc_tab, compiler->labels))
	    ERROR_RETURN_LOG(int, "Cannot patch the the labels");

	return rc;
}

int lang_compiler_validate(const lang_compiler_t* compiler)
{
	if(NULL == compiler)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != compiler->error)
	    ERROR_RETURN_LOG(int, "Compiler error has been previously set");

	if(bitmask_empty(compiler->regmap) <= 0)
	    ERROR_RETURN_LOG(int, "Register leak detected");

	if(NULL != compiler->current_block)
	    ERROR_RETURN_LOG(int, "Block statement not closed yet");

	return 0;
}

lang_compiler_error_t* lang_compiler_get_error(const lang_compiler_t* compiler)
{
	if(NULL == compiler) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return compiler->error;
}
