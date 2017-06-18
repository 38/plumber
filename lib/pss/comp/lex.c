/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include <package_config.h>

#include <error.h>

#include <pss/log.h>
#include <pss/comp/lex.h>

/**
 * @brief the actual data structure for a lexer
 **/
struct _pss_comp_lex_t {
	uint32_t error:1;   /*!< if there's an error */
	uint32_t line;      /*!< current line number */
	uint32_t offset;    /*!< current line offset */
	uint32_t buffer_next;  /*!< the begin position of the uncosumed data */
	uint32_t buffer_limit; /*!< the position where the valid data ends in the buffer */
	const char* errstr;          /*!< the error string used to describe this error */
	char filename[PATH_MAX + 1]; /*!< the filename used for error displaying */
	const char* buffer; /*!< the buffer used to read the script file */
};

/**
 * @brief create a new lexer object
 * @return the newly create lexer, NULL on error
 **/
static inline pss_comp_lex_t* _lexer_new()
{
	pss_comp_lex_t* ret = NULL;
	ret = (pss_comp_lex_t*)malloc(sizeof(pss_comp_lex_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory");

	ret->error = 0;
	ret->line = 0;
	ret->offset = 0;
	ret->buffer_next = 0;
	ret->buffer_limit = 0;
	ret->buffer = NULL;

	return ret;
}

/**
 * @brief dispose a used lexer object
 * @param lexer the lexer to dispose
 * @return the status code
 **/
static inline int _lexer_free(pss_comp_lex_t* lexer)
{
	if(NULL == lexer) ERROR_RETURN_LOG(int, "Invalid arguments");
	free(lexer);
	return 0;
}


pss_comp_lex_t* pss_comp_lex_new(const char* filename, const char* buffer, uint32_t size)
{
	if(NULL == buffer) ERROR_PTR_RETURN_LOG("Invalid arguments");

	pss_comp_lex_t* ret = _lexer_new();
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create new lexer object");

	ret->buffer = buffer;
	ret->buffer_limit = size;
	snprintf(ret->filename, sizeof(ret->filename), "%s", filename);

	return ret;
}

int pss_comp_lex_free(pss_comp_lex_t* lexer)
{
	if(NULL == lexer) ERROR_RETURN_LOG(int, "Invalid arguments");

	return _lexer_free(lexer);
}

static inline int _peek(pss_comp_lex_t* lexer, uint32_t n)
{
	if(lexer->buffer_next + n >= lexer->buffer_limit)
	    return ERROR_CODE(int);
	return lexer->buffer[lexer->buffer_next + n];
}
static inline void _consume(pss_comp_lex_t* lexer, uint32_t n)
{
	for(;n > 0; n --)
	{
		if(lexer->buffer_next >= lexer->buffer_limit) return;
		char ret = lexer->buffer[lexer->buffer_next ++];
		lexer->offset ++;
		if(('\r' == ret && _peek(lexer, 0) != '\n') || ('\n' == ret))
		{
			lexer->offset = 0;
			lexer->line ++;
		}
	}
}
static inline void _ws(pss_comp_lex_t* lexer)
{
	int rc;
	for(;;_consume(lexer, 1))
	{
		rc = _peek(lexer, 0);
		if(rc == '\t' || rc == ' ' || rc == '\r' || rc == '\n') continue;
		return;
	}
}
static inline void _line(pss_comp_lex_t* lexer)
{
	uint32_t current_line = lexer->line;
	for(;current_line == lexer->line && _peek(lexer, 0) != ERROR_CODE(int); _consume(lexer, 1));
}
static inline int _comments_or_ws(pss_comp_lex_t* lexer)
{
	for(;;)
	{
		_ws(lexer);
		if(_peek(lexer, 0) == '#') _line(lexer);
		else if(_peek(lexer, 0) == '/')
		{
			if(_peek(lexer, 1) == '/') _line(lexer);
			else if(_peek(lexer, 1) == '*')
			{
				int state = 0;
				_consume(lexer, 2);
				for(;state != 2; _consume(lexer,1))
				{
					int next = _peek(lexer, 1);
					if(next == -1) break;
					else if(0 == state && '*' == next) state ++;
					else if(1 == state && '/' == next) state ++;
				}
				if(state != 2)
				{
					lexer->error = 1;
					lexer->errstr = "Unexpected EOF in comment block";
					return ERROR_CODE(int);
				}
				else _consume(lexer, 1);
			}
			else return 0;
		}
		else return 0;
	}
}
static inline int _in_id_charset(int ch)
{
	if(ch >= 'a' && ch <= 'z') return 1;
	if(ch >= 'A' && ch <= 'Z') return 1;
	if(ch >= '0' && ch <= '9') return 1;
	if(ch == '_') return 1;
	if(ch == '$') return 1;
	return 0;
}

static inline int _hex_digit_to_val(int ch)
{
	if(ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
	if(ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
	if(ch >= '0' && ch <= '9') return ch - '0';
	return ERROR_CODE(int);
}

static inline int _dec_digit_to_val(int ch)
{
	if(ch >= '0' && ch <= '9') return ch - '0';
	return ERROR_CODE(int);
}

static inline int _oct_digit_to_val(int ch)
{
	if(ch >= '0' && ch <= '7') return ch - '0';
	return ERROR_CODE(int);
}

static inline int _match(pss_comp_lex_t* lexer, const char* str)
{
	uint32_t i = 0;
	for(;*str;str++, i ++)
	    if(_peek(lexer, i) != *str)
	        return 0;
	if(_in_id_charset(_peek(lexer, i))) return 0;
	_consume(lexer, i);
	return 1;
}

static inline pss_comp_lex_keyword_t _id_or_keyword(pss_comp_lex_t* lexer, char* buffer, size_t size)
{
	if(_match(lexer, "start")) return PSS_COMP_LEX_KEYWORD_START;
	if(_match(lexer, "visualize")) return PSS_COMP_LEX_KEYWORD_VISUALIZE;
	if(_match(lexer, "echo")) return PSS_COMP_LEX_KEYWORD_ECHO;
	if(_match(lexer, "include")) return PSS_COMP_LEX_KEYWORD_INCLUDE;
	if(_match(lexer, "if")) return PSS_COMP_LEX_KEYWORD_IF;
	if(_match(lexer, "else")) return PSS_COMP_LEX_KEYWORD_ELSE;
	if(_match(lexer, "insmod")) return PSS_COMP_LEX_KEYWORD_INSMOD;
	if(_match(lexer, "var")) return PSS_COMP_LEX_KEYWORD_VAR;
	if(_match(lexer, "while")) return PSS_COMP_LEX_KEYWORD_WHILE;
	if(_match(lexer, "for")) return PSS_COMP_LEX_KEYWORD_FOR;
	if(_match(lexer, "break")) return PSS_COMP_LEX_KEYWORD_BREAK;
	if(_match(lexer, "continue")) return PSS_COMP_LEX_KEYWORD_CONTINUE;
	if(_match(lexer, "undefined")) return PSS_COMP_LEX_KEYWORD_UNDEFINED;
	if(_match(lexer, "function")) return PSS_COMP_LEX_KEYWORD_FUNCTION;

	int warnned = 0, len = 0;

	for(;_in_id_charset(_peek(lexer, 0)); _consume(lexer, 1))
	    if(size > 1)
	        *(buffer ++) = (char)_peek(lexer, 0), size --, len ++;
	    else if(!warnned)
	    {
		    LOG_WARNING("identifer truncated");
		    warnned = 1;
	    }
	buffer[0] = 0;
	if(len == 0)
	{
		lexer->error = 1;
		lexer->errstr = "Invalid identifier";
	}
	return PSS_COMP_LEX_KEYWORD_ERROR;
}

static inline int _num(pss_comp_lex_t* lex, int32_t* result)
{
	int32_t current;
	if(_peek(lex, 0) == '0')
	{
		if(_peek(lex, 1) == 'x')
		{
			/* hex */
			_consume(lex, 2);
			*result = 0;
			for(;(current = _hex_digit_to_val(_peek(lex, 0))) != ERROR_CODE(int); _consume(lex, 1))
			    *result = (*result) * 16 + current;
			return 0;
		}
		else if((current = _oct_digit_to_val(_peek(lex, 1))) != ERROR_CODE(int))
		{
			/* oct */
			_consume(lex, 2);
			*result = current;
			for(;(current = _oct_digit_to_val(_peek(lex, 0))) != ERROR_CODE(int); _consume(lex, 1))
			    *result = (*result) * 8 + current;
		}
		else
		{
			_consume(lex, 1);
			*result = 0;
		}
	}
	else if((current = _dec_digit_to_val(_peek(lex, 0))) != ERROR_CODE(int))
	{
		/*dec*/
		*result = current;
		_consume(lex, 1);
		for(;(current = _dec_digit_to_val(_peek(lex, 0))) != ERROR_CODE(int); _consume(lex, 1))
		    *result = (*result) * 10 + current;
	}

	return 0;
}

static inline int _str(pss_comp_lex_t* lex, char* buf, size_t sz)
{
	if(_peek(lex, 0) != '"') return ERROR_CODE(int);
	_consume(lex, 1);
	enum {
		_NORMAL_STR,
		_ESC_BEGIN,
		_ESC_OCT_1,
		_ESC_OCT_2,
		_ESC_HEX,
		_STR_END,
		_ERROR
	} state = _NORMAL_STR;
	int ch, trancated = 0;
	int esc_chr = 0;

	//int actual_char[2] = {-1, -1};
	int underlying_char = -1;
	int reparse = 0;
	for(;state != _STR_END && state != _ERROR && (ch = _peek(lex, 0)) != ERROR_CODE(int); _consume(lex, 1))
	{
PARSE:
		underlying_char = -1;
		reparse = 0;
		//actual_char[0] = actual_char[1] = -1;
		switch(state)
		{
			case _NORMAL_STR:
			    if(ch == '\\') state = _ESC_BEGIN;
			    else if(ch == '"') state = _STR_END;
			    else underlying_char = ch;
			    break;
			case _ESC_BEGIN:
			    switch(ch)
			    {
				    case 'a': underlying_char = '\a';  state = _NORMAL_STR; break;
				    case 'b': underlying_char = '\b';  state = _NORMAL_STR; break;
				    case 'f': underlying_char = '\f';  state = _NORMAL_STR; break;
				    case 'n': underlying_char = '\n';  state = _NORMAL_STR; break;
				    case 'r': underlying_char = '\r';  state = _NORMAL_STR; break;
				    case 't': underlying_char = '\t';  state = _NORMAL_STR; break;
				    case 'v': underlying_char = '\v';  state = _NORMAL_STR; break;
				    case '\\': underlying_char = '\\'; state = _NORMAL_STR; break;
				    case '\'': underlying_char = '\''; state = _NORMAL_STR; break;
				    case '\?': underlying_char = '\?'; state = _NORMAL_STR; break;
				    case '\"': underlying_char = '\"'; state = _NORMAL_STR; break;
				    default:
				        if(ch >= '0' && ch <= '8')
				        {
					        esc_chr = _oct_digit_to_val(ch);
					        state = _ESC_OCT_1;
				        }
				        else if(ch == 'x')
				        {
					        esc_chr = 0;
					        state = _ESC_HEX;
				        }
				        else
				        {
					        lex->error = 1;
					        lex->errstr = "Invalid escape sequence";
					        state = _ERROR;
				        }
			    }
			    break;
			case _ESC_OCT_1:
			    if(ch >= '0' && ch <= '8')
			    {
				    esc_chr = esc_chr * 8 + _oct_digit_to_val(ch);
				    state = _ESC_OCT_2;
			    }
			    else
			    {
				    state = _NORMAL_STR;
				    underlying_char = esc_chr;
				    reparse = 1;
			    }
			    break;
			case _ESC_OCT_2:
			    if(ch >= '0' && ch <= '8')
			    {
				    esc_chr = esc_chr * 8 + _oct_digit_to_val(ch);
				    state = _NORMAL_STR;
				    underlying_char = esc_chr;
			    }
			    else
			    {
				    state = _NORMAL_STR;
				    underlying_char = esc_chr;
				    reparse = 1;
			    }
			    break;
			case _ESC_HEX:
			    if(_hex_digit_to_val(ch) != ERROR_CODE(int))
			    {
				    int val = _hex_digit_to_val(ch);
				    esc_chr = esc_chr * 16 + val;
			    }
			    else
			    {
				    state = _NORMAL_STR;
				    underlying_char = esc_chr;
				    reparse = 1;
			    }
			    break;
			case _STR_END: break;
			case _ERROR: break;
		}

		if(underlying_char >= 0)
		{
			if(sz > 1) *(buf++) = (char)underlying_char, sz --;
			else if(!trancated)
			{
				LOG_WARNING("string trancated");
				trancated = 1;
			}
			if(reparse == 1) goto PARSE;
		}
	}

	buf[0] = 0;
	if(_ERROR == state) return ERROR_CODE(int);

	return 0;
}
static inline int _graphviz_prop(pss_comp_lex_t* lexer, char* buf, size_t sz)
{
	if(_peek(lexer, 0) != '@' || _peek(lexer, 1) != '[') return ERROR_CODE(int);
	_consume(lexer, 2);
	int level = 1, ch, truncated = 0;
	enum {
		_DOT_CODE,
		_DOT_STRING,
		_DOT_ESC
	} state = _DOT_CODE;
	for(;(ch = _peek(lexer, 0)) != ERROR_CODE(int) && level > 0; _consume(lexer, 1))
	{
		switch(state)
		{
			case _DOT_CODE:
			    if(ch == '"') state = _DOT_STRING;
			    else if(ch == '[') level ++;
			    else if(ch == ']') level --;
			    break;
			case _DOT_STRING:
			    if(ch == '\\') state = _DOT_ESC;
			    else if(ch == '"') state = _DOT_CODE;
			    break;
			case _DOT_ESC:
			    state = _DOT_STRING;
		}
		if(level > 0)
		{
			if(sz > 1)
			    *(buf ++) = (char)ch, sz --;
			else if(!truncated)
			{
				LOG_WARNING("Graphviz code truncated");
				truncated = 1;
			}
		}
	}
	buf[0] = 0;
	if(level > 0) return ERROR_CODE(int);
	return 0;
}
int pss_comp_lex_next_token(pss_comp_lex_t* lexer, pss_comp_lex_token_t* buffer)
{
	if(NULL == lexer || NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(_comments_or_ws(lexer) == ERROR_CODE(int))
	{
		LOG_DEBUG("cannot trip the comments");
		lexer->error = 1;
		lexer->errstr = "Invalid comment block, unexpected EOF";
		return 0;
	}

	buffer->file = lexer->filename;
	buffer->line = lexer->line;
	buffer->offset = lexer->offset;

	int ch = _peek(lexer, 0);

	switch(ch)
	{
#define _LEX_CASE(ch, token_type) case (ch): buffer->type = token_type;
#define _LEX_SINGLE_CHAR_TOKEN(ch, token_type) _LEX_CASE(ch, token_type) {\
			_consume(lexer, 1);\
			break;\
		}
		_LEX_SINGLE_CHAR_TOKEN(-1, PSS_COMP_LEX_TOKEN_EOF);
		_LEX_SINGLE_CHAR_TOKEN('(', PSS_COMP_LEX_TOKEN_LPARENTHESIS);
		_LEX_SINGLE_CHAR_TOKEN(')', PSS_COMP_LEX_TOKEN_RPARENTHESIS);
		_LEX_SINGLE_CHAR_TOKEN('{', PSS_COMP_LEX_TOKEN_LBRACE);
		_LEX_SINGLE_CHAR_TOKEN('}', PSS_COMP_LEX_TOKEN_RBRACE);
		_LEX_SINGLE_CHAR_TOKEN(';', PSS_COMP_LEX_TOKEN_SEMICOLON);
		_LEX_SINGLE_CHAR_TOKEN('.', PSS_COMP_LEX_TOKEN_DOT);
		_LEX_SINGLE_CHAR_TOKEN(',', PSS_COMP_LEX_TOKEN_COMMA);
		_LEX_CASE('&', PSS_COMP_LEX_TOKEN_AND)
		{
			if(_peek(lexer, 1) == '&')
			{
				_consume(lexer, 2);
			}
			else
			{
				lexer->error = 1;
				lexer->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE('|', PSS_COMP_LEX_TOKEN_OR)
		{
			if(_peek(lexer, 1) == '|')
			{
				_consume(lexer, 2);
			}
			else
			{
				lexer->error = 1;
				lexer->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE('!', PSS_COMP_LEX_TOKEN_NE)
		{
			if(_peek(lexer, 1) == '=')
			{
				_consume(lexer, 2);
			}
			else
			{
				buffer->type = PSS_COMP_LEX_TOKEN_NOT;
				_consume(lexer, 1);
			}
			break;
		}
		_LEX_CASE('=', PSS_COMP_LEX_TOKEN_EQUAL)
		{
			if(_peek(lexer, 1) == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_EQUALEQUAL;
				_consume(lexer, 2);
			}
			else
			{
				_consume(lexer, 1);
			}
			break;
		}
		_LEX_CASE('>', PSS_COMP_LEX_TOKEN_GT)
		{
			if(_peek(lexer, 1) == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_GE;
				_consume(lexer, 2);
			}
			else if(_peek(lexer, 1) == '>' && _peek(lexer,2) == '>')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_TRIPLE_GT;
				_consume(lexer, 3);
			}
			else
			{
				_consume(lexer, 1);
			}
			break;
		}
		_LEX_CASE('<', PSS_COMP_LEX_TOKEN_LT)
		{
			if(_peek(lexer, 1) == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_LE;
				_consume(lexer, 2);
			}
			else
			{
				_consume(lexer, 1);
			}
			break;
		}
		_LEX_CASE('@', PSS_COMP_LEX_TOKEN_GRAPHVIZ_PROP)
		{
			if(_peek(lexer, 1) == '[')
			{
				if(_graphviz_prop(lexer, buffer->value.s, sizeof(buffer->value.s)) == ERROR_CODE(int))
				{
					lexer->error = 1;
					lexer->errstr = "Invalid Graphviz property";
				}
			}
			else
			{
				lexer->error = 1;
				lexer->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE(':', PSS_COMP_LEX_TOKEN_COLON_EQUAL)
		{
			if(_peek(lexer, 1) == '=')
			{
				_consume(lexer, 2);
			}
			else
			{
				lexer->error = 1;
				lexer->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE('+', PSS_COMP_LEX_TOKEN_ADD)
		{
			int next = _peek(lexer, 1);
			if(next == '+')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_INCREASE;
				_consume(lexer, 2);
			}
			else if(next == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_ADD_EQUAL;
				_consume(lexer, 2);
			}
			else
			    _consume(lexer, 1);
			break;
		}
		_LEX_CASE('-', PSS_COMP_LEX_TOKEN_MINUS)
		{
			int next = _peek(lexer, 1);
			if(next == '>')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_ARROW;
				_consume(lexer, 2);
			}
			else if(next == '-')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_DECREASE;
				_consume(lexer, 2);
			}
			else if(next == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_MINUS_EQUAL;
				_consume(lexer, 2);
			}
			else
			    _consume(lexer, 1);
			break;
		}
		_LEX_CASE('*', PSS_COMP_LEX_TOKEN_TIMES)
		{
			int next = _peek(lexer, 1);
			if(next == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_TIMES_EQUAL;
				_consume(lexer, 2);
			}
			else
			    _consume(lexer, 1);
			break;
		}
		_LEX_CASE('/', PSS_COMP_LEX_TOKEN_DIVIDE)
		{
			int next = _peek(lexer, 1);
			if(next == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_DIVIDE_EQUAL;
				_consume(lexer, 2);
			}
			else
			    _consume(lexer, 1);
			break;
		}
		_LEX_CASE('%', PSS_COMP_LEX_TOKEN_MODULAR)
		{
			int next = _peek(lexer, 1);
			if(next == '=')
			{
				buffer->type = PSS_COMP_LEX_TOKEN_MODULAR_EQUAL;
				_consume(lexer, 2);
			}
			else
			    _consume(lexer, 1);
			break;
		}
		_LEX_CASE('"', PSS_COMP_LEX_TOKEN_STRING)
		{
			if(_str(lexer, buffer->value.s, sizeof(buffer->value.s)) == ERROR_CODE(int))
			{
				lexer->error = 1;
				lexer->errstr = "Invalid string";
			}
			break;
		}
		default:
		    if(ch >= '0' && ch <= '9')
		    {
			    if(_num(lexer, &buffer->value.i) == ERROR_CODE(int))
			    {
				    lexer->error = 1;
				    lexer->errstr = "Invalid number";
			    }
			    buffer->type = PSS_COMP_LEX_TOKEN_INTEGER;
			    break;
		    }
		    else
		    {
			    pss_comp_lex_keyword_t token = _id_or_keyword(lexer, buffer->value.s, sizeof(buffer->value.s));
			    if(token == PSS_COMP_LEX_KEYWORD_ERROR)
			    {
				    buffer->type = PSS_COMP_LEX_TOKEN_IDENTIFIER;
			    }
			    else
			    {
				    buffer->type = PSS_COMP_LEX_TOKEN_KEYWORD;
				    buffer->value.k = token;
			    }
		    }
	}

	if(1 == lexer->error)
	{
		LOG_DEBUG("Detected an lexical error in file %s, line %u, offset %u", lexer->filename, lexer->line + 1, lexer->offset + 1);
		buffer->type = PSS_COMP_LEX_TOKEN_ERROR;
		buffer->value.e = lexer->errstr;
		lexer->error = 0;
	}

	return 0;
}
