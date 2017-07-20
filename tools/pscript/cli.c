/**
 * Copyright (C) 2017, Feng Liu
 **/
#include <stdio.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <utils/log.h>

#include <pss.h>

#include <plumber.h>
#include <builtin.h>
#include <cli.h>

extern pss_vm_t* current_vm;

static const char* _prompt = "PSS> ";

static int _interrupt = 0;

struct _line_list_t {
	char *line;
	uint32_t off;
	uint32_t size;
	struct _line_list_t *next;
};

/** @brief concatenate the lines in line list
 */
static char* _cat_lines(struct _line_list_t *head, uint32_t code_size)
{
	if(NULL == head || 0 >= code_size)
	    return NULL;

	char* code = (char*)malloc((size_t)code_size);
	if(NULL == code)
	    return NULL;
	struct _line_list_t *node = head;
	while(node)
	{
		if(NULL == node->line)
		    continue;
		memcpy(code + node->off, node->line, node->size);
		// add '\n' in the end of each line
		*(code + node->off + node->size) = '\n';
		node = node->next;
	}
	// but do not add '\n' in the end
	*(code + code_size - 1) = 0;
	return code;
}

/** @brief free the line list
 */
static void _free_line_list(struct _line_list_t *head)
{
	struct _line_list_t *pre;
	while(head)
	{
		pre = head;
		head = head->next;
		free(pre);
	}
}

#define MAX_BRACKETS 256

#define _CHECK_BRACKETS_TOP(left) \
    if(0 == b_index || left != bracket_stack[b_index - 1]) \
        ERROR_RETURN_ACTION(int, b_index = 0); \
    else \
        b_index--; \
    break;

/** @brief simply check the syntax of code by analyzing the brackets pairs
 * @param lexer lexer should be valid
 * @return positive if need more inputs, 0 if input complete,
 * ERROR_CODE(int) if error occurs
 * @note remember to reset b_index every time when error occurs
 **/
static int _scan_brackets(pss_comp_lex_t* lexer)
{
	static pss_comp_lex_token_type_t bracket_stack[MAX_BRACKETS];
	static int b_index;
	pss_comp_lex_token_t token;
	while(1)
	{
		if(ERROR_CODE(int) == pss_comp_lex_next_token(lexer, &token))
		{
			ERROR_RETURN_ACTION(int, b_index = 0);
		}
		switch(token.type)
		{
			case PSS_COMP_LEX_TOKEN_LPARENTHESIS:
			case PSS_COMP_LEX_TOKEN_LBRACKET:
			case PSS_COMP_LEX_TOKEN_LBRACE:
			    bracket_stack[b_index++] = token.type;
			    break;
			case PSS_COMP_LEX_TOKEN_RPARENTHESIS:
			    _CHECK_BRACKETS_TOP(PSS_COMP_LEX_TOKEN_LPARENTHESIS)
			case PSS_COMP_LEX_TOKEN_RBRACKET:
			    _CHECK_BRACKETS_TOP(PSS_COMP_LEX_TOKEN_LBRACKET)
			case PSS_COMP_LEX_TOKEN_RBRACE:
			    _CHECK_BRACKETS_TOP(PSS_COMP_LEX_TOKEN_LBRACE)
			case PSS_COMP_LEX_TOKEN_ERROR:
			    ERROR_RETURN_ACTION(int, b_index = 0);
			case PSS_COMP_LEX_TOKEN_EOF:
			    return b_index;
			default:
			    /* Ignore */
			    break;
		}
		// check stack size
		if(b_index >= MAX_BRACKETS)
		{
			LOG_ERROR("Code too long");
			ERROR_RETURN_ACTION(int, b_index = 0);
		}
	}
	return b_index;
}

/** @brief add one line to the line_list
 * @return the new head
 */
static struct _line_list_t* _add_line(struct _line_list_t* head, char* line, uint32_t size, uint32_t off)
{
	if(NULL == line)
	    return head;

	struct _line_list_t *node = (struct _line_list_t*)malloc(sizeof(*node));
	if(NULL == node)
	    return NULL;
	node->line = line;
	node->size = size;
	node->off = off;
	node->next = head;
	return node;
}

static void _stop(int signo)
{
	(void)signo;
	LOG_DEBUG("SIGINT Caught!");
	_interrupt = 1;
}

int pss_cli_interactive(uint32_t debug)
{
	static const char* source_path = "_";
	char* line = (char*)NULL;
	pss_comp_lex_t* lexer = NULL;
	pss_bytecode_module_t* module = NULL;
	pss_comp_error_t* err = NULL;
	struct _line_list_t *head = NULL;
	char *code;
	uint32_t lex_success;

	current_vm = pss_vm_new();
	if(NULL == current_vm || ERROR_CODE(int) == builtin_init(current_vm))
	{
		if(current_vm != NULL) pss_vm_free(current_vm);
		LOG_ERROR("Cannot create PSS Virtual Machine");
		return 1;
	}
	uint32_t code_size;

	signal(SIGINT, _stop);

	while(1)
	{
		line = readline(_prompt);
		// ignore empty line
		if(NULL == line)
		    continue;
		if(0 == *line)
		{
			free(line);
			continue;
		}

		// quit TODO: builtin commands
		if('q' == *line && 0 == *(line + 1))
		{
			free(line);
			return 0;
		}

		code_size = 0;
		head = NULL;
		module = NULL;
		lexer = NULL;
		lex_success = 0;
		err = NULL;

		while(NULL != line && 0 == _interrupt)
		{
			uint32_t line_size = (uint32_t)strlen(line);
			head = _add_line(head, line, line_size, code_size);
			if(NULL == head)
			{
				LOG_ERROR("malloc _line_list_t failed");
				break;
			}
			// should add a newline at the end of each line
			code_size += line_size + 1;
			// lexical analysis of a line of code
			if(NULL == (lexer = pss_comp_lex_new(source_path, line, line_size + 1)))
			{
				LOG_ERROR("Syntax error!");
				break;
			}
			int scan_ret = _scan_brackets(lexer);
			if(ERROR_CODE(int) == scan_ret)
			{
				LOG_ERROR("Syntax error!");
				break;
			}
			pss_comp_lex_free(lexer);
			lexer = NULL;
			// While a piece of code is finished, compile and execute it
			if(0 == scan_ret)
			{
				lex_success = 1;
				break;
			}
			line = readline(NULL);
		}
		code = _cat_lines(head, code_size);
		if(NULL == code)
		    goto _END_OF_CODE;
		add_history(code);
		if(lex_success)
		{
			if(NULL == (module = pss_bytecode_module_new()))
			    ERROR_LOG_GOTO(_END_OF_CODE, "Cannot create module instance");
			lexer = pss_comp_lex_new("stdin", code, (unsigned)code_size);
			pss_comp_option_t opt = {
				.lexer = lexer,
				.module = module,
				.debug = (debug != 0)
			};
			if(ERROR_CODE(int) == pss_comp_compile(&opt, &err))
			    ERROR_LOG_GOTO(_END_OF_CODE, "Cannot compile the source code");
			int rc = pss_vm_run_module(current_vm, module, NULL);
			LOG_INFO("VM terminated with exit code %d", rc);
			if(ERROR_CODE(int) == rc)
			{
				pss_vm_exception_t* exception = pss_vm_last_exception(current_vm);
				LOG_ERROR("PSS VM Exception: %s", exception->message);
				pss_vm_backtrace_t* backtrace = exception->backtrace;
				LOG_ERROR("======Stack backtrace begin ========");
				print_bt(backtrace);
				LOG_ERROR("======Stack backtrace end   ========");
				pss_vm_exception_free(exception);
			}
		}
_END_OF_CODE:
		if(NULL != err)
		{
			const pss_comp_error_t* this;
			for(this = err; NULL != this; this = this->next)
			    fprintf(stderr, "%u:%u:error: %s\n", this->line + 1,
			            this->column + 1, this->message);
			pss_comp_free_error(err);
		}
		if(NULL != code) free(code);
		if(NULL != lexer) pss_comp_lex_free(lexer);
		if(NULL != module) pss_bytecode_module_free(module);
		_free_line_list(head);
		_interrupt = 0;
	}
	return 0;
}
