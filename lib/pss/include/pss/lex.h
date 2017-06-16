/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The lexer of the Plumber Service Script
 * @file pss/include/pss/lex.h
 **/
#ifndef __PSS_LEX_H__
#define __PSS_LEX_H__

/**
 * @brief the token types of the lexer
 **/
typedef enum {
	PSS_LEX_TOKEN_NAT = -2,    /*!< Not A Token */
	PSS_LEX_TOKEN_ERROR = -1,  /*!< means a lexical error */
	PSS_LEX_TOKEN_KEYWORD,     /*!< a keyword */
	PSS_LEX_TOKEN_IDENTIFIER,  /*!< a identifier */
	PSS_LEX_TOKEN_INTEGER,     /*!< a integer literal */
	PSS_LEX_TOKEN_STRING,      /*!< a string literal */
	PSS_LEX_TOKEN_EQUAL,       /*!< = */
	PSS_LEX_TOKEN_COLON_EQUAL, /*!< := */
	PSS_LEX_TOKEN_LPARENTHESIS,/*!< ( */
	PSS_LEX_TOKEN_RPARENTHESIS,/*!< ) */
	PSS_LEX_TOKEN_GRAPHVIZ_PROP, /*!< a graphviz property [....] */
	PSS_LEX_TOKEN_LBRACE,      /*!< { */
	PSS_LEX_TOKEN_RBRACE,      /*!< } */
	PSS_LEX_TOKEN_LT,          /*!< &lt; */
	PSS_LEX_TOKEN_LE,          /*!< &lt;= */
	PSS_LEX_TOKEN_GT,          /*!< &gt; */
	PSS_LEX_TOKEN_TRIPLE_GT,   /*!< &gt;&gt;&gt */
	PSS_LEX_TOKEN_GE,          /*!< &gt;= */
	PSS_LEX_TOKEN_EQUALEQUAL,  /*!< == */
	PSS_LEX_TOKEN_NE,          /*!< != */
	PSS_LEX_TOKEN_NOT,         /*!< ! */
	PSS_LEX_TOKEN_AND,         /*!< && */
	PSS_LEX_TOKEN_OR,          /*!< || */
	PSS_LEX_TOKEN_ARROW,       /*!< -&gt; */
	PSS_LEX_TOKEN_SEMICOLON,   /*!< ; */
	PSS_LEX_TOKEN_DOT,         /*!< dot */
	PSS_LEX_TOKEN_ADD,         /*!< + */
	PSS_LEX_TOKEN_ADD_EQUAL,   /*!< += */
	PSS_LEX_TOKEN_INCREASE,    /*!< ++ */
	PSS_LEX_TOKEN_MINUS,       /*!< - */
	PSS_LEX_TOKEN_MINUS_EQUAL, /*!< -= */
	PSS_LEX_TOKEN_DECREASE,    /*!< -- */
	PSS_LEX_TOKEN_TIMES,       /*!< * */
	PSS_LEX_TOKEN_TIMES_EQUAL, /*!< *= */
	PSS_LEX_TOKEN_DIVIDE,      /*!< / */
	PSS_LEX_TOKEN_DIVIDE_EQUAL,/*!< /= */
	PSS_LEX_TOKEN_MODULAR,     /*!< % */
	PSS_LEX_TOKEN_MODULAR_EQUAL, /*!< %= */
	PSS_LEX_TOKEN_COMMA,       /*!< , */
	PSS_LEX_TOKEN_EOF,          /*!< end of file */
	PSS_LEX_TOKEN_NUM_OF_ENTRIES /*!< the number of entries */
} pss_lex_token_type_t;

/**
 * @brief the keyword enum
 **/
typedef enum {
	PSS_LEX_KEYWORD_ERROR = -1,
	PSS_LEX_KEYWORD_ECHO,
	PSS_LEX_KEYWORD_VISUALIZE,
	PSS_LEX_KEYWORD_START,
	PSS_LEX_KEYWORD_INCLUDE,
	PSS_LEX_KEYWORD_INSMOD,
	PSS_LEX_KEYWORD_IF,          /* if */
	PSS_LEX_KEYWORD_ELSE,        /* else  */
	PSS_LEX_KEYWORD_FUNCTION,
	PSS_LEX_KEYWORD_VAR,
	PSS_LEX_KEYWORD_WHILE,
	PSS_LEX_KEYWORD_FOR,
	PSS_LEX_KEYWORD_BREAK,
	PSS_LEX_KEYWORD_CONTINUE,
	PSS_LEX_KEYWORD_UNDEFINED
} pss_lex_keyword_t;

/**
 * @brief represents a token
 **/
typedef struct {
	const char* file;                       /*!< the file name of this token */
	uint32_t    line;                       /*!< the line number of this token in the source code */
	uint32_t    offset;                     /*!< the offset of this token in the source code*/
	pss_lex_token_type_t type;             /*!< the type of this token */
	union {
		int32_t i;                          /*!< the integer value of this token */
		char s[4096];                       /*!< the string value of this token */
		const char* e;                      /*!< the error message */
		pss_lex_keyword_t k;               /*!< the keyword data */
	} value;
} pss_lex_token_t;

/**
 * @brief the incomplete type for a lexer
 **/
typedef struct _pss_lex_t pss_lex_t;

/**
 * @brief create a new plumber lexer
 * @param filename the filename used to display the error message
 * @param buffer The input buffer
 * @param size The size
 * @return the newly created lexer
 **/
pss_lex_t* pss_lex_from_buffer(const char* filename, const char* buffer, uint32_t size);

/**
 * @brief dispose a used lexer
 * @param lexer the target lexer
 * @return the status code
 **/
int pss_lex_free(pss_lex_t* lexer);

/**
 * @brief get the next token from the lexer
 * @param lexer the target lexer
 * @param buffer the token buffer
 * @return the status code
 **/
int pss_lex_next_token(pss_lex_t* lexer, pss_lex_token_t* buffer);

/**
 * @brief include a script
 * @param lexer the top-level lexer
 * @param filename the target filename to include
 * @return status code
 **/
int pss_lex_include_script(pss_lex_t* lexer, const char* filename);

#endif
