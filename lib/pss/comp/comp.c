/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

#include <predict.h>
#include <error.h>

#include <package_config.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/comp/lex.h>
#include <pss/comp/env.h>
#include <pss/comp/comp.h>

#include <pss/comp/block.h>

#define _LOOKAHEAD 3

/**
 * @brief Represent a control block info
 **/
typedef struct {
	pss_bytecode_segment_t*  seg;      /*!< The code segment which contains this control block */
	pss_bytecode_addr_t      begin;    /*!< The begin address inside the code block */
	pss_bytecode_label_t     end;      /*!< The end label of the code block */
	uint32_t                 loop:1;   /*!< If this block is a loop */
} _control_block_t;

/**
 * @brief The actual data structure for a compiler instance
 **/
struct _pss_comp_t {
	uint32_t                 last_line;        /*!< The last line number */
	uint32_t                 debug:1;          /*!< If we need to emit the debug information */
	pss_comp_lex_t*          lexer;            /*!< The lexer we are using */
	pss_bytecode_module_t*   module;           /*!< The output bytecode module */
	pss_comp_env_t*          env;              /*!< The compile time environment abstraction */
	pss_comp_error_t**       error_buf;        /*!< The last compiler error */
	pss_comp_lex_token_t     ahead[_LOOKAHEAD];/*!< The lookahead ring buffer */
	uint32_t                 ahead_begin;      /*!< The ring buffer begin */
	pss_bytecode_segment_t*  seg_stack[PSS_COMP_ENV_SCOPE_MAX];  /*!< The current code segment the compiler is writting */
	char*                    seg_name_stack[PSS_COMP_ENV_SCOPE_MAX]; /*!< The segment name stack */
	uint32_t                 seg_stack_top;    /*!< The stack top for the current segment ID */
	_control_block_t         ctl_stack[PSS_COMP_ENV_SCOPE_MAX];    /*!< The control block stack */
	uint32_t                 ctl_stack_top;/*!< The top pointer of the control block stack */
	uint32_t                 last_consumed_line; /*!< The line number of the last consumed token */
};

int pss_comp_compile(pss_comp_option_t* option, pss_comp_error_t** error)
{
	int rc = ERROR_CODE(int);
	if(NULL == option || NULL == option->lexer || NULL == option->module)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_comp_t compiler = {
		.lexer = option->lexer,
		.module = option->module,
		.error_buf = error,
		.ahead_begin = 0,
		.debug = option->debug,
		.last_line = 0,
		.last_consumed_line = ERROR_CODE(uint32_t)
	};

	pss_bytecode_segid_t entry_point;

	uint32_t i;
	for(i = 0; i < _LOOKAHEAD; i ++)
	compiler.ahead[i].type = PSS_COMP_LEX_TOKEN_NAT;

	if(NULL == (compiler.env = pss_comp_env_new()))
	    return pss_comp_raise(&compiler, "Internal error: Cannot current compile time environment");


	if(ERROR_CODE(int) == pss_comp_open_closure(&compiler, pss_comp_lex_get_filename(compiler.lexer), 0, NULL)) goto ERR;

	if(ERROR_CODE(int) == pss_comp_block_parse(&compiler, PSS_COMP_LEX_TOKEN_NAT, PSS_COMP_LEX_TOKEN_EOF, option->repl)) goto ERR;

	if(ERROR_CODE(pss_bytecode_regid_t) == (entry_point = pss_comp_close_closure(&compiler))) goto ERR;

	if(ERROR_CODE(int) == pss_bytecode_module_set_entry_point(compiler.module, entry_point))
	    rc = pss_comp_raise(&compiler, "Internal error: Cannot set the entry point for current module");
	else rc = 0;

ERR:
	while(compiler.seg_stack_top > 0)
	{
		if(NULL != compiler.seg_name_stack[compiler.seg_stack_top-1])
		    free(compiler.seg_name_stack[compiler.seg_stack_top-1]);
		pss_bytecode_segment_free(compiler.seg_stack[--compiler.seg_stack_top]);
	}
	if(NULL != compiler.env) pss_comp_env_free(compiler.env);
	return rc;
}

int pss_comp_raise(pss_comp_t* comp, const char* msg, ...)
{
	if(NULL == comp || NULL == msg)
	    ERROR_LOG_GOTO(ERR, "Invalid arguments");

	va_list ap;
	va_start(ap, msg);
	int bytes_required = vsnprintf(NULL, 0, msg, ap);

	pss_comp_error_t* err = (pss_comp_error_t*)malloc(sizeof(pss_comp_error_t) + (unsigned)bytes_required + 1);
	if(NULL == err)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the error message");
	vsnprintf(err->message, (unsigned)bytes_required + 1, msg, ap);
	err->next = *comp->error_buf;
	*comp->error_buf = err;

	const pss_comp_lex_token_t* recent = pss_comp_peek(comp, 0);
	if(recent != NULL)
	{
		err->filename = recent->file;
		err->line = recent->line;
		err->column = recent->offset;
	}
	else
	{
		err->filename = "<unknown>";
		err->line = 0;
		err->column = 0;
	}

	va_end(ap);
ERR:
	return ERROR_CODE(int);
}

int pss_comp_raise_internal(pss_comp_t* comp, pss_comp_internal_t reason)
{
	if(NULL == comp) ERROR_RETURN_LOG(int, "Internal error: Invalid arguments");

	switch(reason)
	{
		case PSS_COMP_INTERNAL_CODE:
		    pss_comp_raise(comp, "Internal error: Cannot append bytecode to the target instruction table");
		    break;
		case PSS_COMP_INTERNAL_ARGS:
		    pss_comp_raise(comp, "Internal error: The compilation function has recieved invalid arguments");
		    break;
		case PSS_COMP_INTERNAL_BUG:
		    pss_comp_raise(comp, "Internal error: Compiler bug");
		    break;
		case PSS_COMP_INTERNAL_SEG:
		    pss_comp_raise(comp, "Internal error: Cannot get current code segment to output");
		    break;
		case PSS_COMP_INTERNAL_MALLOC:
		    pss_comp_raise(comp, "Internal error: Cannot allocate memory from the OS [errno: %d]", errno);
		    break;
		default:
		    LOG_ERROR("Internal error: Invalid reason code");
	}

	return ERROR_CODE(int);
}

int pss_comp_free_error(pss_comp_error_t* error)
{
	if(NULL == error) ERROR_RETURN_LOG(int, "Invalid arguments");

	pss_comp_error_t* this;
	for(;NULL != error;)
	{
		this = error;
		error = error->next;
		free(this);
	}

	return 0;
}

const pss_comp_lex_token_t* pss_comp_peek(pss_comp_t* comp, uint32_t n)
{
	if(NULL == comp || n >= _LOOKAHEAD)
	{
		pss_comp_raise(comp, "Internal error: Invalid arguments");
		return NULL;
	}

	uint32_t i;
	for(i = 0; i <= n; i ++)
	{
		uint32_t off = (i + comp->ahead_begin) % _LOOKAHEAD;
		if(comp->ahead[off].type == PSS_COMP_LEX_TOKEN_NAT)
		{
			if(pss_comp_lex_next_token(comp->lexer, comp->ahead + off) == ERROR_CODE(int))
			{
				pss_comp_raise(comp, "Internal error: Cannot peek the token ahead");
				return NULL;
			}
		}
	}

	const pss_comp_lex_token_t* next_token = comp->ahead + comp->ahead_begin % _LOOKAHEAD;
	if(comp->debug && next_token->type != PSS_COMP_LEX_TOKEN_NAT && next_token->line + 1 != comp->last_line)
	{
		pss_bytecode_segment_t* seg = comp->seg_stack[comp->seg_stack_top - 1];
		comp->last_line = next_token->line + 1;
		if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_DINFO_LINE,
		                                                                            PSS_BYTECODE_ARG_NUMERIC(next_token->line + 1),
		                                                                            PSS_BYTECODE_ARG_END))
		{
			pss_comp_raise(comp, "Internal error: Cannot intert the debugging info");
			return NULL;
		}
	}

	return comp->ahead + (n + comp->ahead_begin) % _LOOKAHEAD;
}

int pss_comp_consume(pss_comp_t* comp, uint32_t n)
{
	if(n > _LOOKAHEAD)
	    return pss_comp_raise(comp, "Internal error: Consuming %u tokens ahead is not allowed", n);

	uint32_t i;
	comp->last_consumed_line = comp->ahead[(comp->ahead_begin + n - 1) % _LOOKAHEAD].line;

	for(i = 0; i < n; i ++)
	    comp->ahead[(i + comp->ahead_begin) % _LOOKAHEAD].type = PSS_COMP_LEX_TOKEN_NAT;
	comp->ahead_begin = (comp->ahead_begin + n) % _LOOKAHEAD;
	return 0;
}

uint32_t pss_comp_last_consumed_line(pss_comp_t* comp)
{
	if(NULL == comp)
	{
		pss_comp_raise(comp, "Internal error: Invalid arguments");
		return ERROR_CODE(uint32_t);
	}

	return comp->last_consumed_line;
}

pss_bytecode_segment_t* pss_comp_get_code_segment(pss_comp_t* comp)
{
	if(NULL == comp)
	{
		pss_comp_raise(comp, "Internal error: Invalid arguments");
		return NULL;
	}

	if(comp->seg_stack_top == 0)
	{
		pss_comp_raise(comp, "Internal error: Compiler is currently out of closure scope");
		return NULL;
	}

	return comp->seg_stack[comp->seg_stack_top - 1];
}

int pss_comp_open_closure(pss_comp_t* comp, const char* id, uint32_t nargs, char const* const* argnames)
{
	if(NULL == comp || ERROR_CODE(uint32_t) == nargs || (NULL == argnames && nargs > 0))
	    PSS_COMP_RAISE_INT(comp, ARGS);

	if(ERROR_CODE(int) == pss_comp_env_open_scope(comp->env))
	    return pss_comp_raise(comp, "Internal error: Cannot open the new closure scope");

	PREDICT_IMPOSSIBLE(nargs > PSS_VM_ARG_MAX);
	pss_bytecode_regid_t argid[nargs == 0 ? 1 : nargs];
	pss_bytecode_segment_t* segment = NULL;
	char* name = NULL;
	uint32_t i;
	for(i = 0; i < nargs; i ++)
	    if(1 != pss_comp_env_get_var(comp->env, argnames[i], 1, argid + i))
	    {
		    pss_comp_raise(comp, "Internal error: Cannot allocate registers for the argument list");
		    goto ERR;
	    }

	if(NULL == (segment = pss_bytecode_segment_new((pss_bytecode_regid_t)nargs, argid)))
	{
		pss_comp_raise(comp, "Internal error: Cannot create code segment for the new function body");
		goto ERR;
	}

	comp->seg_stack[comp->seg_stack_top] = segment;
	if(NULL != id)
	{
		if(NULL == (name = comp->seg_name_stack[comp->seg_stack_top] = strdup(id)))
		{
			pss_comp_raise(comp, "Internal error: Cannot duplicate the function name string");
			goto ERR;
		}
	}
	else comp->seg_name_stack[comp->seg_stack_top] = NULL;

	if(comp->debug)
	{
		char full_name[4096];
		char* ptr = full_name;
		uint32_t j;
		for(j = 0; j <= comp->seg_stack_top; j ++)
		{
			size_t bufsize = sizeof(full_name) - (size_t)(ptr - full_name);
			size_t rc = (size_t)snprintf(ptr, bufsize, (j?"@%s":"%s"), comp->seg_name_stack[j] ? comp->seg_name_stack[j] : "<Anonymous>");
			if(rc > bufsize) rc = bufsize - 1;
			bufsize -= rc;
			ptr += rc;
		}
		if(ERROR_CODE(pss_bytecode_addr_t) ==
		   pss_bytecode_segment_append_code(segment,
		        PSS_BYTECODE_OPCODE_DINFO_FUNC,
		        PSS_BYTECODE_ARG_STRING(full_name),
		        PSS_BYTECODE_ARG_END))
		{
			pss_comp_raise(comp, "Internal error: Cannot append debug info to the code segment");
			goto ERR;
		}
	}

	comp->seg_stack_top ++;

	return 0;
ERR:
	pss_comp_env_close_scope(comp->env);
	if(name != NULL)
	    free(name);
	if(segment != NULL)
	    pss_bytecode_segment_free(segment);
	return ERROR_CODE(int);
}

pss_bytecode_segid_t pss_comp_close_closure(pss_comp_t* comp)
{
	if(NULL == comp)
	    return (pss_bytecode_segid_t)pss_comp_raise(comp, "Internal error: Invalid arguments");

	if(comp->seg_stack_top == 0)
	    return (pss_bytecode_segid_t)pss_comp_raise(comp, "Internal error: Compiler is currently out of any closure");

	if(ERROR_CODE(int) == pss_comp_env_close_scope(comp->env))
	    return (pss_bytecode_segid_t)pss_comp_raise(comp, "Internal error: Cannot close the current closure scope");

	pss_bytecode_segment_t* seg = comp->seg_stack[comp->seg_stack_top - 1];

	/* Finally, we need to append the return sentinel to the segment, so that the VM won't get to somewhere undefined */
	pss_bytecode_addr_t guard_addr;
	guard_addr = pss_bytecode_segment_length(seg);
	if(ERROR_CODE(pss_bytecode_addr_t) == guard_addr)
	    return (pss_bytecode_segid_t)pss_comp_raise(comp, "Internal error: Cannot get the length of the code segment");

	do{
		pss_bytecode_instruction_t inst;
		if(guard_addr > 0 && ERROR_CODE(int) != pss_bytecode_segment_get_inst(seg, guard_addr - 1, &inst) &&
		   inst.info->operation == PSS_BYTECODE_OP_RETURN) break;

		if(ERROR_CODE(pss_bytecode_addr_t) == (guard_addr = pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_UNDEF_LOAD, PSS_BYTECODE_ARG_REGISTER(0), PSS_BYTECODE_ARG_END)))
		    return (pss_bytecode_segid_t)pss_comp_raise(comp, "Internal error: Cannot append the sentinel instruction to the code segment");
		if(ERROR_CODE(pss_bytecode_addr_t) == pss_bytecode_segment_append_code(seg, PSS_BYTECODE_OPCODE_RETURN, PSS_BYTECODE_ARG_REGISTER(0), PSS_BYTECODE_ARG_END))
		    return (pss_bytecode_segid_t)pss_comp_raise(comp, "Internal error: Cannot append the sentinel instruction to the code segment");
	} while(0);

	for(;comp->ctl_stack_top > 0 &&
	     comp->ctl_stack[comp->ctl_stack_top - 1].seg == seg;
	     comp->ctl_stack_top --)
	    if(ERROR_CODE(int) == pss_bytecode_segment_patch_label(seg, comp->ctl_stack[comp->ctl_stack_top - 1].end, guard_addr))
	        return (pss_bytecode_addr_t)pss_comp_raise(comp, "Internal error: Cannot apply the label address");

	if(NULL != comp->seg_name_stack[comp->seg_stack_top-1])
	    free(comp->seg_name_stack[comp->seg_stack_top-1]);

	pss_bytecode_segid_t ret = pss_bytecode_module_append(comp->module, comp->seg_stack[--comp->seg_stack_top]);
	if(ERROR_CODE(pss_bytecode_segid_t) == ret)
	    pss_comp_raise(comp,  "Internal error: Cannot append the compiled code segment to module");

	return ret;
}

int pss_comp_open_control_block(pss_comp_t* comp, int loop)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get the code segment for current compiler context");

	pss_bytecode_addr_t begin = pss_bytecode_segment_length(seg);
	if(ERROR_CODE(pss_bytecode_addr_t) == begin)
	    return pss_comp_raise(comp, "Internal error: Cannot get the length of the code segment");

	pss_bytecode_label_t end = pss_bytecode_segment_label_alloc(seg);
	if(ERROR_CODE(pss_bytecode_label_t) == end)
	    return pss_comp_raise(comp, "Internal error: Cannot allocate new label");

	comp->ctl_stack[comp->ctl_stack_top].begin = begin;
	comp->ctl_stack[comp->ctl_stack_top].end   = end;
	comp->ctl_stack[comp->ctl_stack_top].seg   = seg;
	comp->ctl_stack[comp->ctl_stack_top].loop  = loop ? 1u : 0u;
	comp->ctl_stack_top ++;

	return 0;
}

int pss_comp_close_control_block(pss_comp_t* comp)
{
	if(NULL == comp)  PSS_COMP_RAISE_INT(comp, ARGS);

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);

	if(NULL == seg) ERROR_RETURN_LOG(int, "Cannot get the code segment for current compiler context");
	if(0 == comp->ctl_stack_top || comp->ctl_stack[comp->ctl_stack_top - 1].seg != seg)
	    return pss_comp_raise(comp, "Internal error: The compiler is current out of any control block");

	pss_bytecode_addr_t current = pss_bytecode_segment_length(seg);
	if(ERROR_CODE(pss_bytecode_addr_t) == current)
	    return pss_comp_raise(comp, "Internal error: Cannot get current instruction address");

	if(ERROR_CODE(int) == pss_bytecode_segment_patch_label(seg, comp->ctl_stack[comp->ctl_stack_top - 1].end, current))
	    return pss_comp_raise(comp, "Internal error: Cannot patch the label with the known address");

	comp->ctl_stack_top --;

	return 0;
}

pss_bytecode_addr_t pss_comp_last_loop_begin(pss_comp_t* comp)
{
	if(NULL == comp) return (pss_bytecode_addr_t)pss_comp_raise(comp, "Internal error: Invalid arguments");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Cannot get the code segment for current compiler context");

	uint32_t level = comp->ctl_stack_top;
	for(;level > 0 && seg == comp->ctl_stack[level - 1].seg; level --)
	    if(comp->ctl_stack[level - 1].loop)
	        return comp->ctl_stack[level - 1].begin;

	return (pss_bytecode_addr_t)pss_comp_raise(comp, "Internal error: Getting a loop address outside of the loop");
}

pss_bytecode_label_t pss_comp_last_loop_end(pss_comp_t* comp)
{
	if(NULL == comp) return (pss_bytecode_label_t)pss_comp_raise(comp, "Internal error: Invalid arguments");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg)
	    return (pss_bytecode_label_t)pss_comp_raise(comp, "Internal error: Cannot get the current code segment");

	uint32_t level = comp->ctl_stack_top;
	for(;level > 0 && seg == comp->ctl_stack[level - 1].seg; level --)
	    if(comp->ctl_stack[level - 1].loop)
	        return comp->ctl_stack[level - 1].end;

	return (pss_bytecode_label_t)pss_comp_raise(comp, "Internal error: Getting a loop address outside of the loop");
}

pss_bytecode_addr_t pss_comp_last_control_block_begin(pss_comp_t* comp, uint32_t n)
{
	if(NULL == comp || comp->ctl_stack_top <= n)
	    return (pss_bytecode_addr_t)pss_comp_raise(comp, "Internal error: Invalid arguments");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);
	if(NULL == seg) ERROR_RETURN_LOG(pss_bytecode_addr_t, "Cannot get the code segment for current compiler context");

	if(0 == comp->ctl_stack_top || seg != comp->ctl_stack[comp->ctl_stack_top - 1].seg)
	    return (pss_bytecode_addr_t)pss_comp_raise(comp, "Internal Error: The compiler is currently out of control block");

	return comp->ctl_stack[comp->ctl_stack_top - 1 - n].begin;
}

pss_bytecode_label_t pss_comp_last_control_block_end(pss_comp_t* comp, uint32_t n)
{
	if(NULL == comp || comp->ctl_stack_top <= n)
	    return (pss_bytecode_label_t)pss_comp_raise(comp, "Internal error: Invalid arguments");

	pss_bytecode_segment_t* seg = pss_comp_get_code_segment(comp);

	if(NULL == seg) ERROR_RETURN_LOG(pss_bytecode_label_t, "Cannot get the code segment for current compiler context");
	if(0 == comp->ctl_stack_top || seg != comp->ctl_stack[comp->ctl_stack_top - 1].seg)
	    return (pss_bytecode_label_t)pss_comp_raise(comp, "Internal Error: The compiler is currently out of control block");

	return comp->ctl_stack[comp->ctl_stack_top - 1 - n].end;
}

pss_comp_env_t* pss_comp_get_env(pss_comp_t* comp)
{
	if(NULL == comp || NULL == comp->env)
	{
		pss_comp_raise(comp, "Internal error: Invalid arguments");
		return NULL;
	}

	return comp->env;
}

int pss_comp_open_scope(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	if(ERROR_CODE(int) == pss_comp_env_open_scope(comp->env))
	    return pss_comp_raise(comp, "Internal error: Cannot open a new scope in the compiler environment");

	return 0;
}

int pss_comp_close_scope(pss_comp_t* comp)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	if(ERROR_CODE(int) == pss_comp_env_close_scope(comp->env))
	    return pss_comp_raise(comp, "Internal error: Cannot close a new scope in the compiler environment");

	return 0;
}

int pss_comp_get_local_var(pss_comp_t* comp, const char* var, pss_bytecode_regid_t* resbuf)
{
	if(NULL == comp || NULL == var) PSS_COMP_RAISE_INT(comp, ARGS);

	int rc = pss_comp_env_get_var(comp->env, var, 0, resbuf);
	if(ERROR_CODE(int) == rc) return pss_comp_raise(comp, "Internal error: Cannot get the variable from environment");

	return rc;
}

pss_bytecode_regid_t pss_comp_decl_local_var(pss_comp_t* comp, const char* var)
{
	if(NULL == comp || NULL == var) return (pss_bytecode_regid_t)pss_comp_raise(comp, "Internal error: Invalid arguments");

	pss_bytecode_regid_t regid;

	int rc = pss_comp_env_get_var(comp->env, var, 1, &regid);
	if(ERROR_CODE(int) == rc)
	    return (pss_bytecode_regid_t)pss_comp_raise(comp, "Internal error: Cannot declare new variable in current scope");

	return regid;
}

pss_bytecode_regid_t pss_comp_mktmp(pss_comp_t* comp)
{
	if(NULL == comp) return (pss_bytecode_regid_t)pss_comp_raise(comp, "Internal error: Invalid arguments");

	pss_bytecode_regid_t ret = pss_comp_env_mktmp(comp->env);
	if(ERROR_CODE(pss_bytecode_regid_t) == ret)
	    return (pss_bytecode_regid_t)pss_comp_raise(comp, "Internal error: Cannot allocate a temp register from the environment");
	return ret;
}

int pss_comp_rmtmp(pss_comp_t* comp, pss_bytecode_regid_t regid)
{
	if(NULL == comp || ERROR_CODE(pss_bytecode_regid_t) == regid)
	    PSS_COMP_RAISE_INT(comp, ARGS);

	if(ERROR_CODE(int) == pss_comp_env_rmtmp(comp->env, regid))
	    return pss_comp_raise(comp, "Internal error: Cannot release the temp register");

	return 0;
}

int pss_comp_expect_token(pss_comp_t* comp, pss_comp_lex_token_type_t token)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	const pss_comp_lex_token_t* next = pss_comp_peek(comp, 0);
	if(NULL == next) ERROR_RETURN_LOG(int, "Cannot peek the next token");

	if(next->type != token)
	    PSS_COMP_RAISE_SYN(int, comp, "Unexpected token");

	if(ERROR_CODE(int) == pss_comp_consume(comp, 1))
	    ERROR_RETURN_LOG(int, "Cannot consume the last token");

	return 0;
}

int pss_comp_expect_keyword(pss_comp_t* comp, pss_comp_lex_keyword_t keyword)
{
	if(NULL == comp) PSS_COMP_RAISE_INT(comp, ARGS);

	const pss_comp_lex_token_t* next = pss_comp_peek(comp, 0);

	if(NULL == next) ERROR_RETURN_LOG(int, "Cannot peek the next token");

	if(next->type != PSS_COMP_LEX_TOKEN_KEYWORD)
	    PSS_COMP_RAISE_SYN(int, comp, "Unexpected token, keyword expected");

	if(next->value.k != keyword)
	    PSS_COMP_RAISE_SYN(int, comp, "Unexpected keyword");

	return pss_comp_consume(comp, 1);
}
