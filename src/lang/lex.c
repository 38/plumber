/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <plumber.h>
#include <utils/log.h>
#include <error.h>
#include <utils/vector.h>
#include <utils/string.h>
#include <utils/static_assertion.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

/**
 * @brief the actual data structure for a lexer
 **/
struct _lang_lex_t {
	uint32_t error:1;   /*!< if there's an error */
	uint32_t level;     /*!< indicates this is what level in the include stack */
	uint32_t line;      /*!< current line number */
	uint32_t offset;    /*!< current line offset */
	uint32_t buffer_next;  /*!< the begin position of the uncosumed data */
	uint32_t buffer_limit; /*!< the position where the valid data ends in the buffer */
	union {
		lang_lex_t* recent_include;     /*!< for the top level file, point to the top of the include stack */
		lang_lex_t* parent_include;  /*!< for the non-top level file, this is the parent file that include this file */
	};
	const char* errstr;          /*!< the error string used to describe this error */
	char filename[PATH_MAX + 1]; /*!< the filename used for error displaying */
	char* buffer; /*!< the buffer used to read the script file */
};

static vector_t* _search_path = NULL;
/**
 * @brief search for the script in the search path
 * @param basename the basename of the script (inlcude the extension name)
 * @param buffer the buffer used to store the path
 * @note this function assume that the buffer has at least PATH_MAX + 1 bytes avaialble
 * @return the path to the found path or NULL if there's no such file
 **/
static inline const char* _search_script(const char *basename, char* buffer)
{
	if(NULL == basename || NULL == buffer) ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t i;
	for(i = 0; i < vector_length(_search_path); i ++)
	{
		const char* dirname =  *VECTOR_GET_CONST(const char*, _search_path, i);
		if(NULL == dirname)
		{
			LOG_WARNING("Invalid entry in the script search path list");
			continue;
		}
		string_buffer_t sbuf;
		string_buffer_open(buffer, PATH_MAX + 1, &sbuf);
		if(ERROR_CODE(size_t) == string_buffer_appendf(&sbuf, "%s/%s", dirname, basename))
		    ERROR_PTR_RETURN_LOG("Cannot build the script path");

		const char* result = string_buffer_close(&sbuf);

		if(access(result, R_OK) < 0)
		{
			LOG_DEBUG_ERRNO("file %s can not be accessed", result);
			continue;
		}

		LOG_INFO("Found script file %s", result);

		return result;
	}

	return NULL;
}

/**
 * @brief read the content of the file to a memory buffer
 * @note its the caller's responsibility to dispose the buffer
 * @param filename the target file name to read
 * @param nbytes the buffer used to return how many bytes
 * @return the newly created buffer
 **/
static inline char* _read_file(const char* filename, uint32_t* nbytes)
{
	char* ret = NULL;
	FILE* fp = NULL;
	if(NULL == (fp = fopen(filename, "r")))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open script file %s", filename);

	/* the initial buffer size */
	size_t cap = LANG_LEX_FILE_BUF_INIT_SIZE;
	ret = (char*)malloc(cap);
	uint32_t limit = 0;
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the file content buffer");

	for(;feof(fp) == 0;)
	{
		size_t rc = fread(ret + limit, 1, cap - limit, fp);
		if(ferror(fp) != 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the file `%s'", filename);
		limit += (uint32_t)rc;
		if(limit == cap)
		{
			char* resized = (char*)realloc(ret, cap * 2);
			if(NULL == resized)
			    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resize the script source buffer");
			ret = resized;
			cap = cap * 2;
		}
	}
	fclose(fp);
	fp = NULL;

	if(nbytes != NULL) *nbytes = limit;

	return ret;
ERR:
	if(NULL != ret) free(ret);
	if(NULL != fp) fclose(fp);
	return NULL;
}

/**
 * @brief create a new lexer object
 * @param parent the parent lexer object, if this is the top level lexer object, this param should be NULL
 * @note when a parent param is passed to this function, the caller should ser the top level lexer object' top-level object field
 * @return the newly create lexer, NULL on error
 **/
static inline lang_lex_t* _lexer_new(lang_lex_t* parent)
{
	lang_lex_t* ret = NULL;
	ret = (lang_lex_t*)malloc(sizeof(lang_lex_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory");

	ret->error = 0;
	ret->line = 0;
	ret->offset = 0;
	ret->buffer_next = 0;
	ret->buffer_limit = 0;
	ret->buffer = NULL;

	if(NULL == parent)
	{
		ret->level = 0;
		ret->recent_include = ret;
	}
	else
	{
		ret->level = parent->level + 1;
		ret->parent_include = parent;
	}

	return ret;
}

/**
 * @brief dispose a used lexer object
 * @param lexer the lexer to dispose
 * @return the status code
 **/
static inline int _lexer_free(lang_lex_t* lexer)
{
	if(NULL == lexer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(NULL != lexer->buffer) free(lexer->buffer);
	free(lexer);
	return 0;
}


/**
 * @brief create a new lexer object
 * @param filename the file name of the script to read
 * @param parent the parent lexer object, if this is the top level
 *        lexer object, this param should be NULL
 * @note when a parent param is passed to this function, the caller sohuld set the
 *       top level lexer object's top-level object field
 * return the newly created lexer, NULL when error
 **/
static inline lang_lex_t* _create_lexer(const char* filename, lang_lex_t* parent)
{
	if(NULL == filename) ERROR_PTR_RETURN_LOG("Invalid arguments");

	lang_lex_t* ret = _lexer_new(parent);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create new lexer object");

	const char* path = _search_script(filename, ret->filename);
	if(NULL == path) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find the script filename `%s'", filename);

	LOG_DEBUG("Found script file `%s'", ret->filename);

	ret->buffer = _read_file(ret->filename, &ret->buffer_limit);
	if(NULL == ret->buffer) ERROR_LOG_GOTO(ERR, "Cannot read source code");

	LOG_DEBUG("Script file %s is opened for read", ret->filename);
	return ret;
ERR:
	if(NULL != ret) _lexer_free(ret);
	return NULL;
}

/**
 * @brief create a lexer from a memory buffer
 * @note this must be a top level script, because the include statement can not refer to any memory location
 * @param buffer the buffer used to create the lexer
 * @param size the size of the data buffer
 * @return the result status code
 **/
lang_lex_t* lang_lex_from_buffer(char* buffer, uint32_t size)
{
	if(NULL == buffer) ERROR_PTR_RETURN_LOG("Invalid arguments");

	lang_lex_t* ret = _lexer_new(NULL);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create new lexer object");

	ret->buffer = buffer;
	ret->buffer_limit = size;
	snprintf(ret->filename, sizeof(ret->filename), "<builtin>");

	return ret;
}

int lang_lex_init()
{
	_search_path = vector_new(sizeof(const char*), LANG_LEX_SEARCH_LIST_INIT_SIZE);
	if(NULL == _search_path) ERROR_RETURN_LOG(int, "Cannot create new vector for the script search list");
	return 0;
}

int lang_lex_finalize()
{
	lang_lex_clear_script_search_path();
	if(NULL != _search_path) vector_free(_search_path);
	return 0;
}

int lang_lex_add_script_search_path(const char* path)
{
	if(NULL == path) ERROR_RETURN_LOG(int, "Invalid arguments");

	size_t size = strlen(path);

	if(size > PATH_MAX)
	{
		LOG_WARNING("the search path length is exceeded the  PATH_MAX, trucated");
		size = PATH_MAX;
	}

	char* buf = (char*)malloc(size + 1);
	if(NULL == buf) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory");

	memcpy(buf, path, size);
	buf[size] = 0;

	vector_t* new = vector_append(_search_path, &buf);
	if(NULL == new) ERROR_RETURN_LOG(int, "Cannot append the path to the script search path list");

	_search_path = new;
	LOG_DEBUG("Script search path %s has been added to the list", path);
	return 0;
}

int lang_lex_clear_script_search_path()
{
	size_t i;
	for(i = 0; i < vector_length(_search_path); i ++)
	{
		char* path = *VECTOR_GET_CONST(char*, _search_path, i);
		if(NULL != path) free(path);
	}

	return 0;
}

char const* const* lang_lex_get_script_search_paths()
{
	return VECTOR_GET_CONST(const char*, _search_path, 0);
}

size_t lang_lex_get_num_script_search_paths()
{
	return vector_length(_search_path);
}

lang_lex_t* lang_lex_new(const char* filename)
{
	return _create_lexer(filename, NULL);
}

int lang_lex_free(lang_lex_t* lexer)
{
	if(NULL == lexer) ERROR_RETURN_LOG(int, "Invalid arguments");

	lang_lex_t* ptr;
	for(ptr = lexer->recent_include;;)
	{
		lang_lex_t* tmp = ptr;
		ptr = ptr->parent_include;
		_lexer_free(tmp);
		if(tmp == lexer) break;
	}
	return 0;
}

int lang_lex_include_script(lang_lex_t* lexer, const char* filename)
{
	if(NULL == filename || NULL == lexer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(lexer->level != 0) ERROR_RETURN_LOG(int, "passed a non-top-level lexer object to this function");

	lang_lex_t* current = _create_lexer(filename, lexer->recent_include);
	if(NULL == current) ERROR_RETURN_LOG(int, "Cannot search new lexer object for the included file");

	lexer->recent_include = current;

	lang_lex_t* ptr;
	for(ptr = current->parent_include;; ptr = ptr->parent_include)
	{
		if(strcmp(current->filename, ptr->filename) == 0)
		{
			lang_lex_pop_include_script(lexer);
			ERROR_RETURN_LOG(int, "Found circular depedency");
		}
		if(ptr == lexer) break;
	}

	LOG_DEBUG("file %s has been successfully included", filename);
	return 0;
}
int lang_lex_pop_include_script(lang_lex_t* lexer)
{
	if(lexer->recent_include == lexer) return 0;

	lang_lex_t* tmp = lexer->recent_include;

	lexer->recent_include = tmp->parent_include;

	_lexer_free(tmp);
	return 0;
}
static inline int _peek(lang_lex_t* lexer, uint32_t n)
{
	if(lexer->buffer_next + n >= lexer->buffer_limit)
	    return ERROR_CODE(int);
	return lexer->buffer[lexer->buffer_next + n];
}
static inline void _consume(lang_lex_t* lexer, uint32_t n)
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
static inline void _ws(lang_lex_t* lexer)
{
	int rc;
	for(;;_consume(lexer, 1))
	{
		rc = _peek(lexer, 0);
		if(rc == '\t' || rc == ' ' || rc == '\r' || rc == '\n') continue;
		return;
	}
}
static inline void _line(lang_lex_t* lexer)
{
	uint32_t current_line = lexer->line;
	for(;current_line == lexer->line && _peek(lexer, 0) != ERROR_CODE(int); _consume(lexer, 1));
}
static inline int _comments_or_ws(lang_lex_t* lexer)
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

static inline int _match(lang_lex_t* lexer, const char* str)
{
	uint32_t i = 0;
	for(;*str;str++, i ++)
	    if(_peek(lexer, i) != *str)
	        return 0;
	if(_in_id_charset(_peek(lexer, i))) return 0;
	_consume(lexer, i);
	return 1;
}

static inline lang_lex_keyword_t _id_or_keyword(lang_lex_t* lexer, char* buffer, size_t size)
{
	if(_match(lexer, "start")) return LANG_LEX_KEYWORD_START;
	if(_match(lexer, "visualize")) return LANG_LEX_KEYWORD_VISUALIZE;
	if(_match(lexer, "echo")) return LANG_LEX_KEYWORD_ECHO;
	if(_match(lexer, "include")) return LANG_LEX_KEYWORD_INCLUDE;
	if(_match(lexer, "if")) return LANG_LEX_KEYWORD_IF;
	if(_match(lexer, "else")) return LANG_LEX_KEYWORD_ELSE;
	if(_match(lexer, "insmod")) return LANG_LEX_KEYWORD_INSMOD;
	if(_match(lexer, "var")) return LANG_LEX_KEYWORD_VAR;
	if(_match(lexer, "while")) return LANG_LEX_KEYWORD_WHILE;
	if(_match(lexer, "for")) return LANG_LEX_KEYWORD_FOR;
	if(_match(lexer, "break")) return LANG_LEX_KEYWORD_BREAK;
	if(_match(lexer, "continue")) return LANG_LEX_KEYWORD_CONTINUE;
	if(_match(lexer, "undefined")) return LANG_LEX_KEYWORD_UNDEFINED;

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
	return LANG_LEX_KEYWORD_ERROR;
}

static inline int _num(lang_lex_t* lex, int32_t* result)
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

static inline int _str(lang_lex_t* lex, char* buf, size_t sz)
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
static inline int _graphviz_prop(lang_lex_t* lexer, char* buf, size_t sz)
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
int lang_lex_next_token(lang_lex_t* lexer, lang_lex_token_t* buffer)
{
	if(NULL == lexer || NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(lexer->level != 0) ERROR_RETURN_LOG(int, "The non-top-level function can't be used in this function");

	lang_lex_t* top = lexer->recent_include;

	if(_comments_or_ws(top) == ERROR_CODE(int))
	{
		LOG_DEBUG("cannot trip the comments");
		top->error = 1;
		top->errstr = "Invalid comment block, unexpected EOF";
		return 0;
	}

	buffer->file = top->filename;
	buffer->line = top->line;
	buffer->offset = top->offset;

	int ch = _peek(top, 0);

	switch(ch)
	{
#define _LEX_CASE(ch, token_type) case (ch): buffer->type = token_type;
#define _LEX_SINGLE_CHAR_TOKEN(ch, token_type) _LEX_CASE(ch, token_type) {\
			_consume(top, 1);\
			break;\
		}
		_LEX_SINGLE_CHAR_TOKEN(-1, LANG_LEX_TOKEN_EOF);
		_LEX_SINGLE_CHAR_TOKEN('(', LANG_LEX_TOKEN_LPARENTHESIS);
		_LEX_SINGLE_CHAR_TOKEN(')', LANG_LEX_TOKEN_RPARENTHESIS);
		_LEX_SINGLE_CHAR_TOKEN('{', LANG_LEX_TOKEN_LBRACE);
		_LEX_SINGLE_CHAR_TOKEN('}', LANG_LEX_TOKEN_RBRACE);
		_LEX_SINGLE_CHAR_TOKEN(';', LANG_LEX_TOKEN_SEMICOLON);
		_LEX_SINGLE_CHAR_TOKEN('.', LANG_LEX_TOKEN_DOT);
		_LEX_SINGLE_CHAR_TOKEN(',', LANG_LEX_TOKEN_COMMA);
		_LEX_CASE('&', LANG_LEX_TOKEN_AND)
		{
			if(_peek(top, 1) == '&')
			{
				_consume(top, 2);
			}
			else
			{
				top->error = 1;
				top->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE('|', LANG_LEX_TOKEN_OR)
		{
			if(_peek(top, 1) == '|')
			{
				_consume(top, 2);
			}
			else
			{
				top->error = 1;
				top->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE('!', LANG_LEX_TOKEN_NE)
		{
			if(_peek(top, 1) == '=')
			{
				_consume(top, 2);
			}
			else
			{
				buffer->type = LANG_LEX_TOKEN_NOT;
				_consume(top, 1);
			}
			break;
		}
		_LEX_CASE('=', LANG_LEX_TOKEN_EQUAL)
		{
			if(_peek(top, 1) == '=')
			{
				buffer->type = LANG_LEX_TOKEN_EQUALEQUAL;
				_consume(top, 2);
			}
			else
			{
				_consume(top, 1);
			}
			break;
		}
		_LEX_CASE('>', LANG_LEX_TOKEN_GT)
		{
			if(_peek(top, 1) == '=')
			{
				buffer->type = LANG_LEX_TOKEN_GE;
				_consume(top, 2);
			}
			else if(_peek(top, 1) == '>' && _peek(top,2) == '>')
			{
				buffer->type = LANG_LEX_TOKEN_TRIPLE_GT;
				_consume(top, 3);
			}
			else
			{
				_consume(top, 1);
			}
			break;
		}
		_LEX_CASE('<', LANG_LEX_TOKEN_LT)
		{
			if(_peek(top, 1) == '=')
			{
				buffer->type = LANG_LEX_TOKEN_LE;
				_consume(top, 2);
			}
			else
			{
				_consume(top, 1);
			}
			break;
		}
		_LEX_CASE('@', LANG_LEX_TOKEN_GRAPHVIZ_PROP)
		{
			if(_peek(top, 1) == '[')
			{
				if(_graphviz_prop(top, buffer->value.s, sizeof(buffer->value.s)) == ERROR_CODE(int))
				{
					lexer->error = 1;
					lexer->errstr = "Invalid Graphviz property";
				}
			}
			else
			{
				top->error = 1;
				top->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE(':', LANG_LEX_TOKEN_COLON_EQUAL)
		{
			if(_peek(top, 1) == '=')
			{
				_consume(top, 2);
			}
			else
			{
				top->error = 1;
				top->errstr = "Invalid token";
			}
			break;
		}
		_LEX_CASE('+', LANG_LEX_TOKEN_ADD)
		{
			int next = _peek(top, 1);
			if(next == '+')
			{
				buffer->type = LANG_LEX_TOKEN_INCREASE;
				_consume(top, 2);
			}
			else if(next == '=')
			{
				buffer->type = LANG_LEX_TOKEN_ADD_EQUAL;
				_consume(top, 2);
			}
			else
			    _consume(top, 1);
			break;
		}
		_LEX_CASE('-', LANG_LEX_TOKEN_MINUS)
		{
			int next = _peek(top, 1);
			if(next == '>')
			{
				buffer->type = LANG_LEX_TOKEN_ARROW;
				_consume(top, 2);
			}
			else if(next == '-')
			{
				buffer->type = LANG_LEX_TOKEN_DECREASE;
				_consume(top, 2);
			}
			else if(next == '=')
			{
				buffer->type = LANG_LEX_TOKEN_MINUS_EQUAL;
				_consume(top, 2);
			}
			else
			    _consume(top, 1);
			break;
		}
		_LEX_CASE('*', LANG_LEX_TOKEN_TIMES)
		{
			int next = _peek(top, 1);
			if(next == '=')
			{
				buffer->type = LANG_LEX_TOKEN_TIMES_EQUAL;
				_consume(top, 2);
			}
			else
			    _consume(top, 1);
			break;
		}
		_LEX_CASE('/', LANG_LEX_TOKEN_DIVIDE)
		{
			int next = _peek(top, 1);
			if(next == '=')
			{
				buffer->type = LANG_LEX_TOKEN_DIVIDE_EQUAL;
				_consume(top, 2);
			}
			else
			    _consume(top, 1);
			break;
		}
		_LEX_CASE('%', LANG_LEX_TOKEN_MODULAR)
		{
			int next = _peek(top, 1);
			if(next == '=')
			{
				buffer->type = LANG_LEX_TOKEN_MODULAR_EQUAL;
				_consume(top, 2);
			}
			else
			    _consume(top, 1);
			break;
		}
		_LEX_CASE('"', LANG_LEX_TOKEN_STRING)
		{
			if(_str(top, buffer->value.s, sizeof(buffer->value.s)) == ERROR_CODE(int))
			{
				top->error = 1;
				top->errstr = "Invalid string";
			}
			break;
		}
		default:
		    if(ch >= '0' && ch <= '9')
		    {
			    if(_num(top, &buffer->value.i) == ERROR_CODE(int))
			    {
				    top->error = 1;
				    top->errstr = "Invalid number";
			    }
			    buffer->type = LANG_LEX_TOKEN_INTEGER;
			    break;
		    }
		    else
		    {
			    lang_lex_keyword_t token = _id_or_keyword(top, buffer->value.s, sizeof(buffer->value.s));
			    if(token == LANG_LEX_KEYWORD_ERROR)
			    {
				    buffer->type = LANG_LEX_TOKEN_IDENTIFIER;
			    }
			    else
			    {
				    buffer->type = LANG_LEX_TOKEN_KEYWORD;
				    buffer->value.k = token;
			    }
		    }
	}

	if(1 == top->error)
	{
		LOG_DEBUG("Detected an lexical error in file %s, line %u, offset %u", top->filename, top->line + 1, top->offset + 1);
		buffer->type = LANG_LEX_TOKEN_ERROR;
		buffer->value.e = top->errstr;
		top->error = 0;
	}

	return 0;
}
