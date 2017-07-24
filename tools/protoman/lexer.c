/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <error.h>
#include <log.h>

#include <proto.h>
#include <lexer.h>

/**
 * @brief the human-readable token name
 **/
static const char* _token_name[] = {
	[LEXER_TOKEN_EOF]      = "End-Of-File",
	[LEXER_TOKEN_ID]       = "Identifier",
	[LEXER_TOKEN_NUMBER]   = "Number",
	[LEXER_TOKEN_DOT]      = ".",
	[LEXER_TOKEN_COLON]    = ":",
	[LEXER_TOKEN_SEMICOLON]= ";",
	[LEXER_TOKEN_COMMA]    = ",",
	[LEXER_TOKEN_LBRACE]   = "{",
	[LEXER_TOKEN_RBRACE]   = "}",
	[LEXER_TOKEN_LBRACKET] = "[",
	[LEXER_TOKEN_RBRACKET] = "]",
	[LEXER_TOKEN_EQUAL]    = "=",
	[LEXER_TOKEN_AT]       = "@",
	[LEXER_TOKEN_K_TYPE]   = "Keyword: type",
	[LEXER_TOKEN_K_ALIAS]  = "Keyword: alias",
	[LEXER_TOKEN_K_PACKAGE]= "Keyword: package"
};

/**
 * @brief the actual type for the protocol type languagel lexer
 **/
struct _lexer_t {
	char*    filename;   /*!< the filename of the source code file */
	uint32_t line;       /*!< current line number */
	uint32_t column;     /*!< current colunm */
	const char* next;    /*!< the next char to consume */
	char*    buffer;     /*!< the buffer that contains all the source code text */
};

lexer_t* lexer_new(const char* filename)
{
	if(NULL == filename) ERROR_PTR_RETURN_LOG("Invalid argumnents");

	size_t fn_len = strlen(filename);
	FILE*  fp = fopen(filename, "r");
	long int size;

	if(NULL == fp) ERROR_PTR_RETURN_LOG_ERRNO("Cannot open the source code file %s", filename);

	lexer_t* ret = (lexer_t*)calloc(sizeof(lexer_t), 1);
	if(NULL == ret) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create new lexer instance");

	ret->line = ret->column = 0;

	if(NULL != (ret->filename = (char*)malloc(fn_len + 1)))
	    memcpy(ret->filename, filename, fn_len + 1);
	else ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the filename");

	if(fseek(fp, 0, SEEK_END) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot seek to the end of the file");

	if((size = ftell(fp)) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the size of the file: %s", filename);

	if(fseek(fp, 0, SEEK_SET) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot seek back to the start of the file");

	if(NULL == (ret->buffer = (char*)malloc((size_t)(size + 1))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the source buffer");

	if(size > 0 && fread(ret->buffer, (size_t)size, 1, fp) != 1)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the source file to buffer");

	ret->buffer[size] = 0;
	ret->next = ret->buffer;

	fclose(fp);

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->buffer) free(ret->buffer);
		if(NULL != ret->filename) free(ret->filename);
		free(ret);
	}
	if(NULL != fp)
	    fclose(fp);
	return NULL;
}

int lexer_free(lexer_t* lexer)
{
	if(NULL == lexer) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(lexer->buffer != NULL) free(lexer->buffer);
	if(lexer->filename != NULL) free(lexer->filename);

	free(lexer);
	return 0;
}

/**
 * @brief create a new token with given size and type
 * @details this function will create a new token that contains the token type, the location info
 *         and will allocate memory that fits the data size. <br/>
 *         This function doesn't manipulate any additional data, so you should update additional
 *         data after everything is done
 * @param lexer the lexer instance
 * @param datasize the size of the data section
 * @param type the type of the token
 * @return the newly created token or NULL for error cases
 **/
static inline lexer_token_t* _token_new(const lexer_t* lexer, size_t datasize, lexer_token_type_t type)
{
	lexer_token_t* ret = (lexer_token_t*)malloc(sizeof(lexer_token_t) + datasize);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the lexer token");

	ret->type = type;
	ret->line = lexer->line;
	ret->column = lexer->column;
	ret->file = lexer->filename;
	ret->metadata.flags.plain = 0;
	ret->metadata.size = (uint8_t)(uintptr_t)(((proto_type_atomic_metadata_t*)NULL)->header_end);

	return ret;
}

/**
 * @brief test if the var is in the range [min,max]
 * @param var the variable
 * @param min the lower bound
 * @param max the upper bound
 * @return the testing result
 **/
#define _INRANGE(var, min, max) ((min) <= (var) && (var) <= max)

/**
 * @brief check if the char is a white space
 * @param ch the char
 * @return test result
 **/
static inline int _is_whitespace(int ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

/**
 * @brief check the char if in the range of identifier charset
 * @param ch the char
 * @return test result
 **/
static inline int _is_idcharset(int ch)
{
	return _INRANGE(ch, 'A', 'Z') ||
	       _INRANGE(ch, 'a', 'z') ||
	       _INRANGE(ch, '0', '9') ||
	       ch == '_';
}

/**
 * @brief check if the char is letter
 * @param ch the char to test
 * @return the test result
 **/
static inline int _is_letter(int ch)
{
	return _INRANGE(ch, 'A', 'Z') || _INRANGE(ch, 'a', 'z');
}

/**
 * @brief create a token that represents a lexier identifier, the token id is in range [begin, end)
 * @param lexer current lexer instance
 * @param begin the offset where the id begin
 * @param end the offset where the id ends
 * @return the newly created lexer token or NULL on error cases
 **/
static inline lexer_token_t* _id_token(const lexer_t* lexer, uint32_t begin, uint32_t end)
{
	lexer_token_t* ret = _token_new(lexer, end - begin + 1, LEXER_TOKEN_ID);

	if(NULL == ret) return NULL;

	memcpy(ret->data->id, lexer->buffer + begin, end - begin);
	ret->data->id[end - begin] = 0;

	return ret;
}

/**
 * @brief create a number token
 * @param lexer the lexer instance
 * @param value the value of the number
 * @return the nlewy created lexer token or NULL
 **/
static inline lexer_token_t* _num_token(const lexer_t* lexer, int64_t value)
{
	lexer_token_t* ret = _token_new(lexer, sizeof(int64_t), LEXER_TOKEN_NUMBER);

	if(NULL == ret) return NULL;

	ret->data->number = value;
	return ret;
}

/**
 * @brief create a float token
 * @param lexer the lexer instance
 * @param value the value of the number
 * @return the newly created lexer token or NULL when error
 **/
static inline lexer_token_t* _float_token(const lexer_t* lexer, double value)
{
	lexer_token_t* ret = _token_new(lexer, sizeof(double), LEXER_TOKEN_FLOAT_POINT);

	if(NULL == ret) return NULL;

	ret->data->floatpoint = value;
	return ret;

}

/**
 * @brief create a type primitvie token
 * @param lexer the lexer instance
 * @param size the size of this pritmive
 * @param metadata The type medadata
 * @return the nlewy created lexer token or NULL
 **/
static inline lexer_token_t* _type_primitive_token(const lexer_t* lexer, uint32_t size, proto_type_atomic_metadata_t metadata)
{
	lexer_token_t* ret = _token_new(lexer, sizeof(uint32_t), LEXER_TOKEN_TYPE_PRIMITIVE);

	if(NULL == ret) return NULL;

	ret->data->size = size;
	ret->metadata = metadata;
	return ret;
}

/**
 * @brief create a  lexer token that do not curry any additional data
 * @param lexer the lexer instance
 * @param type the type of the instance
 * @return the newly created token
 **/
static inline lexer_token_t* _const_token(const lexer_t* lexer, lexer_token_type_t type)
{
	return _token_new(lexer, 0, type);
}

/**
 * @brief peek the next charecter, if the end of the data stream is seen, return -1
 * @param lexer the lexer
 * @note this function assume the lexer is a initialized object
 * @return the char or -1
 **/
static inline int _peek(lexer_t* lexer)
{
	if(0 == *lexer->next) return -1;
	return *lexer->next;
}

/**
 * @brief check if the next token in the lexer is the given id
 * @param lexer the lexer instance to check
 * @param id the id string to check
 * @return 1 if there's exactly the given id, 0 when it doesn't match. error code on error cases
 **/
static inline int _peek_ahead(lexer_t* lexer, const char* id)
{
	const char* src = lexer->next;
	int match = 1;
	for(;*id && *src && match; id++, src ++)
	    match &= (*id == *src);
	return (match && *id == 0 && !_is_idcharset(*src));
}


/**
 * @brief move current cursor of the lexer forward to n chars from current location
 *        and maintain the line number and column number up-to-dated
 * @note this function do not have null pointer check
 * @param lexer the lexer
 * @param n the number of chars to consume
 * @return nothing
 **/
static inline void _consume(lexer_t* lexer, uint32_t n)
{
	uint32_t i;
	for(i = 0; i < n; i ++)
	{
		char ch = *lexer->next;
		if(ch == '\n') lexer->line ++, lexer->column = 0;
		else lexer->column ++;
	}
	lexer->next += n;
}

static inline void _strip_whilespace_and_comment(lexer_t* lexer)
{
	int ch = -1;

	for(;;)
	{
		// strip the white space first
		while(_is_whitespace(ch = _peek(lexer)))
		    _consume(lexer, 1);

		//then strip the comment
		if(ch == '#')
		{
			while(ch != '\n' && ch != -1)
			{
				_consume(lexer, 1);
				ch = _peek(lexer);
			}
		}
		else if(ch == '/')
		{
			_consume(lexer, 1);
			ch = _peek(lexer);
			if(ch == '/')
			{
				while(ch != '\n' && ch != -1)
				{
					_consume(lexer, 1);
					ch = _peek(lexer);
				}
			}
			else if(ch == '*')
			{
				_consume(lexer, 1);
				ch = _peek(lexer);
				int state = 0;
				for(;-1 != (ch = _peek(lexer)) && state != 2; _consume(lexer, 1))
				{
					switch(state)
					{
						case 0:
						    if(ch == '*') state = 1;
						    break;
						case 1:
						    if(ch == '/') state = 2;
						    else state = 0;
						    break;
						default:
						    state = 0;
					}
				}
			}
			else return;
		}
		else return;
	}
}

/**
 * @brief parse the next number token
 * @param lexer the lexer instance
 * @note this function assume that it's alread at the head of number token
 * @return the token parsed
 **/
lexer_token_t* _parse_number(lexer_t* lexer)
{
	int ch = _peek(lexer);
	int64_t value = 0;
	int sign = 1;
	while(ch == '-' || ch == '+')
	{
		if(ch == '-') sign = -sign;
		_consume(lexer, 1);
		ch = _peek(lexer);
	}
	if(ch == '0')
	{
		_consume(lexer, 1);
		ch = _peek(lexer);
		if(ch == 'x')
		{
			int valid = 0;
			_consume(lexer, 1);
			for(;;)
			{
				ch = _peek(lexer);
				if(_INRANGE(ch, '0', '9'))
				    value = (value * 16) + (uint32_t)(ch - '0');
				else if(_INRANGE(ch, 'A', 'F'))
				    value = (value * 16) + (uint32_t)(ch - 'A' + 10);
				else if(_INRANGE(ch, 'a', 'f'))
				    value = (value * 16) + (uint32_t)(ch - 'a' + 10);
				else break;

				valid = 1;
				_consume(lexer, 1);
			}

			return valid ? _num_token(lexer, sign * value) : NULL;
		}
		else
		{
			for(;;)
			{
				if(_INRANGE(ch, '0', '7'))
				    value = (value * 8) + (uint32_t)(ch - '0');
				else break;

				_consume(lexer, 1);
				ch = _peek(lexer);
			}

			return _num_token(lexer, sign * value);
		}
	}
	else
	{
		for(;;)
		{
			if(_INRANGE(ch, '0', '9'))
			    value = (value * 10) + (uint32_t)(ch - '0');
			else break;

			_consume(lexer, 1);
			ch = _peek(lexer);
		}

		if(ch != '.' && ch != 'e')
		    return _num_token(lexer, sign * value);
		else
		{
			double fval = (double)value;
			double exp = 1;
			if(ch == '.')
			{
				double mul = 0.1;
				_consume(lexer, 1);
				for(;;)
				{
					ch = _peek(lexer);
					if(_INRANGE(ch, '0', '9'))
					    fval += mul * (ch - '0') * mul;
					else break;
					_consume(lexer, 1);
				}
			}
			if(ch == 'e')
			{
				double mul = 10;
				_consume(lexer, 1);
				for(;;)
				{
					ch = _peek(lexer);
					if(ch == '+') ;
					else if(ch == '-')
					    mul = mul > 1 ? 0.1 : 10.0;
					else break;
					_consume(lexer, 1);
				}

				if(!_INRANGE(ch, '0', '9')) return NULL;

				int pow = 0;
				for(;;)
				{
					if(_INRANGE(ch, '0', '9'))
					    pow = pow * 10 + ch - '0';
					else break;
					_consume(lexer, 1);
					ch = _peek(lexer);
				}

				for(;pow; pow /= 2, mul *= mul)
				    if(pow&1) exp *= mul;
			}
			return _float_token(lexer, sign * exp * fval);
		}
	}

	return NULL;
}

lexer_token_t* lexer_next_token(lexer_t* lexer)
{
	if(NULL == lexer) ERROR_PTR_RETURN_LOG("Invalid arguments");

	_strip_whilespace_and_comment(lexer);

	int ch = _peek(lexer);

	if(ch == -1)
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_EOF);
	}
	else if(ch == '.')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_DOT);
	}
	else if(ch == ':')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_COLON);
	}
	else if(ch == ';')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_SEMICOLON);
	}
	else if(ch == ',')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_COMMA);
	}
	else if(ch == '{')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_LBRACE);
	}
	else if(ch == '}')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_RBRACE);
	}
	else if(ch == '[')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_LBRACKET);
	}
	else if(ch == ']')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_RBRACKET);
	}
	else if(ch == '=')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_EQUAL);
	}
	else if(ch == '@')
	{
		_consume(lexer, 1);
		return _const_token(lexer, LEXER_TOKEN_AT);
	}
	else if(_is_letter(ch) || ch == '_' || ch == '$')
	{
#define KEYWORD(literal, value) \
		else if(_peek_ahead(lexer, literal)) \
		{\
			static const char buf[] = literal;\
			_consume(lexer, sizeof(buf) - 1);\
			return _const_token(lexer, value);\
		}
#define PRITMIVE_TYPE_NUMERIC(literal, size, initializers...) \
		else if(_peek_ahead(lexer, literal)) \
		{\
			static const char buf[] = literal;\
			_consume(lexer, sizeof(buf) - 1);\
			proto_type_atomic_metadata_t metadata = {\
				.flags = {\
					.numeric = {.invalid = 0, ##initializers}\
				}\
			};\
			return _type_primitive_token(lexer, size, metadata);\
		}

#define PRITMIVE_TYPE_SCOPE(literal, size, initializers...) \
		else if(_peek_ahead(lexer, literal)) \
		{\
			static const char buf[] = literal;\
			_consume(lexer, sizeof(buf) - 1);\
			proto_type_atomic_metadata_t metadata = {\
				.flags = {\
					.scope = {.valid = 1, ##initializers}\
				}\
			};\
			return _type_primitive_token(lexer, size, metadata);\
		}
		if(0);
		KEYWORD("type", LEXER_TOKEN_K_TYPE)
		KEYWORD("alias", LEXER_TOKEN_K_ALIAS)
		KEYWORD("package", LEXER_TOKEN_K_PACKAGE)
		PRITMIVE_TYPE_NUMERIC("uint64", 8, .is_real = 0u, .is_signed = 0u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("int64",  8, .is_real = 0u, .is_signed = 1u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("double", 8, .is_real = 1u, .is_signed = 1u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("uint32", 4, .is_real = 0u, .is_signed = 0u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("int32",  4, .is_real = 0u, .is_signed = 1u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("float",  4, .is_real = 1u, .is_signed = 1u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("uint16", 2, .is_real = 0u, .is_signed = 0u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("int16",  2, .is_real = 0u, .is_signed = 1u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("uint8",  1, .is_real = 0u, .is_signed = 0u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("int8",   1, .is_real = 0u, .is_signed = 1u, .default_size = 0u)
		PRITMIVE_TYPE_NUMERIC("char",   1, .is_real = 0u, .is_signed = 1u, .default_size = 0u)
		PRITMIVE_TYPE_SCOPE("request_local_token", sizeof(uint32_t), .primitive = 0u, .typename_size = 0u)
		else
		{
			const char* begin = lexer->next;
			const char* end = lexer->next;
			for(;_is_idcharset(*end);end ++);

			uint32_t begin_ofs = (uint32_t)(begin - lexer->buffer);
			uint32_t end_ofs   = (uint32_t)(end - lexer->buffer);

			_consume(lexer, end_ofs - begin_ofs);

			return _id_token(lexer, begin_ofs, end_ofs);
		}
	}
	else if(_INRANGE(ch, '0', '9') || ch == '-' || ch == '.')
	{
		return _parse_number(lexer);
	}

	LOG_ERROR("%s:%u:%u: error: lexical error: invalid token", lexer->filename, lexer->line + 1, lexer->column + 1);

	return NULL;
}

int lexer_token_free(lexer_token_t* token)
{
	if(NULL == token) ERROR_RETURN_LOG(int, "Invalid arguments");

	free(token);
	return 0;
}

const char* lexer_token_get_name(const lexer_token_t* token)
{
	if(NULL == token) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return _token_name[token->type];
}
