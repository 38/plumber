/**
 * Copyright (C) 2017, Feng Liu
 **/
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <utils/log.h>

#include <pss.h>

#include <plumber.h>
#include <builtin.h>
#include <cli.h>

extern pss_vm_t* current_vm;

#define PSS_CLI_PROMPT "PSS> "
const char* prompt = PSS_CLI_PROMPT;

struct _line_list {
	char *line;
	uint32_t size;
	struct _line_list *next;
};

/** @brief concatenate the lines in line list
 */
char* _cat_lines(struct _line_list *head, uint32_t code_size)
{
	char* code = (char*)malloc((size_t)code_size + 1);
	uint32_t off = 0;
	struct _line_list *node = head->next;
	while(node)
	{
		memcpy(code + off, node->line, node->size);
		off += node->size;
		node = node->next;
	}
	return code;
}

/** @brief free the line list
 */
void _free_line_list(struct _line_list *head)
{
	struct _line_list *p = head->next, *pre;
	head->next = NULL;
	while(p)
	{
		if(p->line) free(p->line);
		pre = p;
		p = p->next;
		free(pre);
	}
}

#define MAX_BRACKETS 256
pss_comp_lex_token_type_t bracket_stack[MAX_BRACKETS];
int b_index;

#define _CHECK_BRACKETS_TOP(left) \
    if(0 == b_index || left != bracket_stack[b_index - 1]) \
        ret = -1; \
    else \
        b_index--; \
    break;

/** @brief simply check the syntax of code by analyzing the brackets pairs
 * @param lexer lexer should be valid
 * @return 0 if success, other if fail
 **/
int _scan_brackets(pss_comp_lex_t* lexer)
{
	pss_comp_lex_token_t token;
	int ret = 0;
	while(0 == ret)
	{
		// TODO: how to check the lexer error. it seems hide some errors inside
		if(ERROR_CODE(int) == pss_comp_lex_next_token(lexer, &token))
		{
			ret = -1;
			break;
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
			    // TODO: needed ?
			    ret = -1;
			    break;
			case PSS_COMP_LEX_TOKEN_EOF:
				return 0;
			default:
			    /* Ignore */
			    break;
		}
		// check stack size
		if(b_index >= MAX_BRACKETS)
		{
			LOG_ERROR("Code too long");
			ret = -1;
			break;
		}
	}
	return ret;
}

/** @brief append one line to the line_list
 * @return the new tail
 */
struct _line_list* _append(struct _line_list* tail, char* line, uint32_t size)
{
	struct _line_list *node = (struct _line_list*)malloc(sizeof(struct _line_list));
	node->line = line;
	node->size = size;
	tail->next = node;
	return node;
}

int pss_cli_interactive()
{
	static const char* source_path = "_";
	char* line = (char*)NULL;
	pss_comp_lex_t* lexer;
	struct _line_list head, *tail = &head;

	current_vm = pss_vm_new();
	if(NULL == current_vm || ERROR_CODE(int) == builtin_init(current_vm))
	{
		if(current_vm != NULL) pss_vm_free(current_vm);
		LOG_ERROR("Cannot create PSS Virtual Machine");
		properly_exit(1);
	}
	uint32_t code_size;
	while(1)
	{
		b_index = 0;
		code_size = 0;
		head.next = NULL;
		tail = &head;

		line = readline(prompt);
		// ignore empty line
		if(NULL != line && 0 == *line)
		{
			free(line);
			continue;
		}

		while(NULL != line)
		{
			uint32_t line_size = (uint32_t)strlen(line);
			tail = _append(tail, line, line_size);
			code_size += line_size;
			// lexical analysis of a line of code
			if(NULL == (lexer = pss_comp_lex_new(source_path, line, line_size + 1)))
			{
				LOG_ERROR("Syntax error!");
				goto _END_OF_CODE;
			}
			if(_scan_brackets(lexer))
			{
				LOG_ERROR("Syntax error!");
				pss_comp_lex_free(lexer);
				goto _END_OF_CODE;
			}
			pss_comp_lex_free(lexer);
			// While a piece of code is finished, compile and execute it
			if(0 == b_index)
			{
				goto _END_OF_CODE;
			}
			line = readline(NULL);
		}
_END_OF_CODE:
		_free_line_list(&head);
	}
}
