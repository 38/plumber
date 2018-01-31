/**
 * Copyright (C) 2017, Feng Liu
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <setjmp.h>

#include <fallthrough.h>
#include <constants.h>

#include <pss.h>
#include <plumber.h>

#include <package_config.h>

#include <builtin.h>
#include <cli.h>
#include <module.h>
#include <barrier.h>

extern pss_vm_t* current_vm;

static int _interrupt = 0;

static int _readline = 0;

static int _service_started = 0;

static int _vm_running = 0;

static sigjmp_buf _restart;

typedef struct _line_list_t {
	char *line;
	uint32_t off;
	uint32_t size;
	struct _line_list_t *next;
} _line_list_t;

/**
 * @brief concatenate the lines in line list
 * @param lines The list of lines has been read
 * @note The line list is in reversed order, and the caller should do cleanup works
 * @return The newly constructed string
 **/
static char* _cat_lines(_line_list_t *lines)
{
	if(NULL == lines) ERROR_PTR_RETURN_LOG("Invalid arguments");

	char* code = (char*)malloc(lines->off + lines->size);
	if(NULL == code) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the code");

	int first = 1;

	while(lines)
	{
		if(NULL == lines->line) continue;
		memcpy(code + lines->off, lines->line, lines->size);
		if(!first) code[lines->off + lines->size - 1] = '\n';
		lines = lines->next;
		first = 0;
	}
	return code;
}

static inline void _print_bt(pss_vm_backtrace_t* bt)
{
	if(NULL == bt) return;
	_print_bt(bt->next);
	fprintf(stderr, "%s, line: %u\n", bt->func, bt->line);
}

/**
 * @brief free the line list
 * @param list The list to dispose
 * @return nothing
 **/
static void _free_line_list(_line_list_t *list)
{
	_line_list_t *pre;
	while(list != NULL)
	{
		pre = list;
		list = list->next;
		free(pre->line);
		free(pre);
	}
}


/**
 * @brief simply check the syntax of code by analyzing the brackets pairs
 * @param lexer lexer should be valid
 * @return positive if need more inputs, 0 if input complete, ERROR_CODE(int) if error occurs
 * @note remember to reset b_index every time when error occurs
 **/
static int _scan_brackets(pss_comp_lex_t* lexer)
{
	static pss_comp_lex_token_type_t bracket_stack[PSCRIPT_CLI_MAX_BRACKET];
	static int b_index;
	pss_comp_lex_token_t token;
	for(;;)
	{
		if(ERROR_CODE(int) == pss_comp_lex_next_token(lexer, &token))
		    ERROR_RETURN_ACTION(int, b_index = 0);
		switch(token.type)
		{
			case PSS_COMP_LEX_TOKEN_LPARENTHESIS:
			    bracket_stack[b_index++] = PSS_COMP_LEX_TOKEN_RPARENTHESIS;
			    break;
			case PSS_COMP_LEX_TOKEN_LBRACKET:
			    bracket_stack[b_index++] = PSS_COMP_LEX_TOKEN_RBRACKET;
			    break;
			case PSS_COMP_LEX_TOKEN_LBRACE:
			    bracket_stack[b_index++] = PSS_COMP_LEX_TOKEN_RBRACE;
			    break;
			case PSS_COMP_LEX_TOKEN_RPARENTHESIS:
			case PSS_COMP_LEX_TOKEN_RBRACKET:
			case PSS_COMP_LEX_TOKEN_RBRACE:
			    if(0 != b_index && token.type == bracket_stack[b_index - 1])
			    {
				    b_index --;
				    break;
			    }
			    b_index = 0;
			    ERROR_RETURN_LOG(int, "Syntax error: unexpected token");
			case PSS_COMP_LEX_TOKEN_ERROR:
			    b_index = 0;
			    ERROR_RETURN_LOG(int, "Lexical error %u:%u: %s", token.line + 1, token.offset + 1, token.value.e);
			case PSS_COMP_LEX_TOKEN_EOF:
			    return b_index;
			default:
			    /* Ignore */
			    break;
		}
		// check stack size
		if(b_index >= (int)(sizeof(bracket_stack) / sizeof(bracket_stack[0])))
		{
			LOG_ERROR("Too many levels of brackets");
			ERROR_RETURN_ACTION(int, b_index = 0);
		}
	}
	return b_index;
}

static void _stop(int signo)
{
	(void)signo;
	if(builtin_service_running())
	{
		LOG_DEBUG("SIGINT Caught!");
		_interrupt = 1;
		sched_loop_kill(1);
	}
	else if(_readline)
	{
		rl_set_signals();
		siglongjmp(_restart, 1);
	}
	else if(_vm_running)
	{
		if(ERROR_CODE(int) == pss_vm_kill(current_vm))
		    LOG_ERROR("Cannot kill the VM");
		fprintf(stderr, "Keyboard Interrupted\n");
	}
}

static pss_value_t _quit(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	(void)argc;
	(void)argv;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_UNDEF
	};

	ret.kind = PSS_VALUE_KIND_UNDEF;

	_service_started = 1;
	_vm_running = 0;
	_interrupt = 1;
	_stop(0);

	return ret;
}

static pss_value_t _help(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_UNDEF
	};

	fprintf(stderr, "\nBuiltin functions:\n\n");

	int print_internal = 0;
	pss_value_builtin_t func = NULL;
	if(argc > 0 && argv[0].kind == PSS_VALUE_KIND_NUM && argv[0].num != 0)
	    print_internal = 1;

	if(argc > 0 && argv[0].kind == PSS_VALUE_KIND_BUILTIN)
	    print_internal = 1, func = argv[0].builtin;

	builtin_print_doc(stderr, print_internal, func);

	fprintf(stderr, "\n");
	return ret;
}

int cli_eval(const char* code, uint32_t debug)
{
	int rc = 1;

	pss_bytecode_module_t* module = NULL;
	pss_comp_error_t* err = NULL;
	pss_value_t result = {};

	current_vm = pss_vm_new();
	if(NULL == current_vm || ERROR_CODE(int) == builtin_init(current_vm))
	{
		if(current_vm != NULL) pss_vm_free(current_vm);
		LOG_ERROR("Cannot create PSS Virtual Machine");
		return 1;
	}

	if(NULL == (module = module_from_buffer(code, (uint32_t)strlen(code), debug, 1)))
	    ERROR_LOG_GOTO(ERR, "Cannot parse code");

	_vm_running = 1;

	rc = pss_vm_run_module(current_vm, module, &result);
	LOG_DEBUG("VM terminated with exit code %d", rc);

	_vm_running = 0;

	if(result.kind != PSS_VALUE_KIND_UNDEF && result.kind != PSS_VALUE_KIND_ERROR)
	{
		char buf[4096];
		if(ERROR_CODE(size_t) == pss_value_strify_to_buf(result, buf, sizeof(buf)))
		    ERROR_LOG_GOTO(ERR, "Type error: Got invalid vlaue");

		printf("%s\n", buf);
	}


	if(ERROR_CODE(int) == rc)
	{
		pss_vm_exception_t* exception = pss_vm_last_exception(current_vm);
		fprintf(stderr, "PSS VM Exception: %s\n\n", exception->message);
		pss_vm_backtrace_t* backtrace = exception->backtrace;
		_print_bt(backtrace);
		fprintf(stderr, "\n");
		pss_vm_exception_free(exception);
		goto ERR;
	}

	if(result.kind != PSS_VALUE_KIND_UNDEF && ERROR_CODE(int) == pss_value_decref(result))
	    ERROR_LOG_GOTO(ERR, "Cannot decref the result value");

	if(NULL != err)
	{
		const pss_comp_error_t* this;
		for(this = err; NULL != this; this = this->next)
		    fprintf(stderr, "%u:%u:error: %s\n", this->line + 1,
		            this->column + 1, this->message);
		pss_comp_free_error(err);
		goto ERR;
	}

	rc = 0;

ERR:

	if(ERROR_CODE(int) == pss_vm_free(current_vm))
	    ERROR_RETURN_LOG(int, "Cannot free current VM");

	if(ERROR_CODE(int) == module_unload_all())
	    ERROR_RETURN_LOG(int, "Cannot unload modules");

	return rc;
}

int cli_interactive(uint32_t debug)
{
	static const char* source_path = "_";
	char* line = (char*)NULL;
	pss_comp_lex_t* lexer = NULL;
	pss_bytecode_module_t* module = NULL;
	pss_comp_error_t* err = NULL;
	static _line_list_t *head = NULL;
	char *code;
	uint32_t lex_success;

	current_vm = pss_vm_new();
	if(NULL == current_vm || ERROR_CODE(int) == builtin_init(current_vm))
	{
		if(current_vm != NULL) pss_vm_free(current_vm);
		LOG_ERROR("Cannot create PSS Virtual Machine");
		return 1;
	}

	pss_value_t value = {
		.kind = PSS_VALUE_KIND_NUM,
		.num = 1
	};

	if(ERROR_CODE(int) == pss_vm_set_global(current_vm, "pscript.repl_mode", value))
	{
		LOG_ERROR("Cannot inject variable pscript.repl_mode to the global");
		return 1;
	}

#define _ADD_BUILTIN_FUNC(name, func) \
	if(ERROR_CODE(int) == pss_vm_add_builtin_func(current_vm, name, func)) \
	{ \
		pss_vm_free(current_vm); \
		LOG_ERROR("Cannot register the "name" builtin"); \
		return 1; \
	}

	_ADD_BUILTIN_FUNC("quit", _quit);
	_ADD_BUILTIN_FUNC("help", _help);

#undef _ADD_BUILTIN_FUNC
	printf("\nREPL Shell for Plumber Service Script\n\nPlumber Version [%s]\n\n", PLUMBER_VERSION);
	printf("help()  -> Get the help message\n");
	printf("quit()  -> Quit the interactive client\n");
	printf("\n");

	signal(SIGINT, _stop);

	if(sigsetjmp(_restart, 1))
	{
		puts("");
	}

	while(!_interrupt)
	{
		_free_line_list(head);
		head = NULL;
		_readline = 1;
		BARRIER();
		line = readline(PSCRIPT_CLI_PROMPT);
		BARRIER();
		_readline = 0;
		// ignore empty line
		if(NULL == line || line[0] == 0)
		{
			if(NULL != line) free(line);
			else goto TERMINATE;
			continue;
		}

		code = NULL;
		head = NULL;
		module = NULL;
		lexer = NULL;
		lex_success = 0;
		err = NULL;

		while(NULL != line && 0 == _interrupt)
		{
			_line_list_t *node = (_line_list_t*)malloc(sizeof(*node));
			if(NULL == node)
			    ERROR_LOG_GOTO(_END_OF_CODE, "Cannot allocate node for the new line");

			uint32_t size = (uint32_t)strlen(line);
			node->line = line;
			node->size = size + 1;
			node->off = (head == NULL ? 0 : head->off + head->size);
			node->next = head;
			head = node;
			line = NULL;

			// lexical analysis of a line of code
			if(NULL == (lexer = pss_comp_lex_new(source_path, head->line, head->size - 1)))
			    ERROR_LOG_GOTO(_ADD_HISTORY, "Cannot create new lexer");

			int scan_ret = _scan_brackets(lexer);
			if(ERROR_CODE(int) == scan_ret)
			    goto _ADD_HISTORY;

			pss_comp_lex_free(lexer);
			lexer = NULL;

			// While a piece of code is finished, compile and execute it
			if(0 == scan_ret)
			{
				lex_success = 1;
				break;
			}

			_readline = 1;
			line = readline(".... ");
			_readline = 0;
		}
_ADD_HISTORY:
		if(NULL == (code = _cat_lines(head))) goto _END_OF_CODE;

		add_history(code);

		pss_value_t result = {};

		if(lex_success)
		{
			if(NULL == (module = module_from_buffer(code, head->off + head->size, debug, 1)))
			    goto _END_OF_CODE;

			_vm_running = 1;

			int rc = pss_vm_run_module(current_vm, module, &result);
			LOG_DEBUG("VM terminated with exit code %d", rc);

			_vm_running = 0;

			if(result.kind != PSS_VALUE_KIND_UNDEF && result.kind != PSS_VALUE_KIND_ERROR)
			{
				char buf[4096];
				if(ERROR_CODE(size_t) == pss_value_strify_to_buf(result, buf, sizeof(buf)))
				{
					LOG_ERROR("Type error: Got invalid value");
					break;
				}

				printf("\033[36m%s\033[0m\n", buf);
			}


			if(ERROR_CODE(int) == rc)
			{
				pss_vm_exception_t* exception = pss_vm_last_exception(current_vm);
				fprintf(stderr, "\n\033[31mPSS VM Exception: %s\n\n", exception->message);
				pss_vm_backtrace_t* backtrace = exception->backtrace;
				_print_bt(backtrace);
				fprintf(stderr, "\033[0m\n");
				pss_vm_exception_free(exception);
			}
		}

_END_OF_CODE:
		if(result.kind != PSS_VALUE_KIND_UNDEF && ERROR_CODE(int) == pss_value_decref(result))
		    LOG_ERROR("Cannot decref the result value");
		if(NULL != line) free(line);
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
		_free_line_list(head);
		head = NULL;
	}
TERMINATE:
	if(ERROR_CODE(int) == pss_vm_free(current_vm))
	    ERROR_RETURN_LOG(int, "Cannot free current VM");

	if(ERROR_CODE(int) == module_unload_all())
	    ERROR_RETURN_LOG(int, "Cannot unload modules");

	return 0;
}
