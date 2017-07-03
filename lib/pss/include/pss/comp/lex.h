/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The lexer of the Plumber Service Script
 * @file pss/include/pss/comp/lex.h
 **/
#ifndef __PSS_COMP_LEX_H__
#define __PSS_COMP_LEX_H__

/**
 * @brief the token types of the lexer
 **/
typedef enum {
	PSS_COMP_LEX_TOKEN_NAT = -2,    /*!< Not A Token */
	PSS_COMP_LEX_TOKEN_ERROR = -1,  /*!< means a lexical error */
	PSS_COMP_LEX_TOKEN_KEYWORD,     /*!< a keyword */
	PSS_COMP_LEX_TOKEN_IDENTIFIER,  /*!< a identifier */
	PSS_COMP_LEX_TOKEN_INTEGER,     /*!< a integer literal */
	PSS_COMP_LEX_TOKEN_STRING,      /*!< a string literal */
	PSS_COMP_LEX_TOKEN_EQUAL,       /*!< = */
	PSS_COMP_LEX_TOKEN_COLON,       /*!< := */
	PSS_COMP_LEX_TOKEN_COLON_EQUAL, /*!< := */
	PSS_COMP_LEX_TOKEN_LPARENTHESIS,/*!< ( */
	PSS_COMP_LEX_TOKEN_RPARENTHESIS,/*!< ) */
	PSS_COMP_LEX_TOKEN_GRAPHVIZ_PROP, /*!< a graphviz property [....] */
	PSS_COMP_LEX_TOKEN_LBRACKET,    /*!< [ */
	PSS_COMP_LEX_TOKEN_RBRACKET,    /*!< ] */
	PSS_COMP_LEX_TOKEN_LBRACE,      /*!< { */
	PSS_COMP_LEX_TOKEN_RBRACE,      /*!< } */
	PSS_COMP_LEX_TOKEN_LT,          /*!< &lt; */
	PSS_COMP_LEX_TOKEN_LE,          /*!< &lt;= */
	PSS_COMP_LEX_TOKEN_GT,          /*!< &gt; */
	PSS_COMP_LEX_TOKEN_TRIPLE_GT,   /*!< &gt;&gt;&gt */
	PSS_COMP_LEX_TOKEN_GE,          /*!< &gt;= */
	PSS_COMP_LEX_TOKEN_EQUALEQUAL,  /*!< == */
	PSS_COMP_LEX_TOKEN_NE,          /*!< != */
	PSS_COMP_LEX_TOKEN_NOT,         /*!< ! */
	PSS_COMP_LEX_TOKEN_AND,         /*!< && */
	PSS_COMP_LEX_TOKEN_OR,          /*!< || */
	PSS_COMP_LEX_TOKEN_ARROW,       /*!< -&gt; */
	PSS_COMP_LEX_TOKEN_SEMICOLON,   /*!< ; */
	PSS_COMP_LEX_TOKEN_ADD,         /*!< + */
	PSS_COMP_LEX_TOKEN_ADD_EQUAL,   /*!< += */
	PSS_COMP_LEX_TOKEN_INCREASE,    /*!< ++ */
	PSS_COMP_LEX_TOKEN_MINUS,       /*!< - */
	PSS_COMP_LEX_TOKEN_MINUS_EQUAL, /*!< -= */
	PSS_COMP_LEX_TOKEN_DECREASE,    /*!< -- */
	PSS_COMP_LEX_TOKEN_TIMES,       /*!< * */
	PSS_COMP_LEX_TOKEN_TIMES_EQUAL, /*!< *= */
	PSS_COMP_LEX_TOKEN_DIVIDE,      /*!< / */
	PSS_COMP_LEX_TOKEN_DIVIDE_EQUAL,/*!< /= */
	PSS_COMP_LEX_TOKEN_MODULAR,     /*!< % */
	PSS_COMP_LEX_TOKEN_MODULAR_EQUAL, /*!< %= */
	PSS_COMP_LEX_TOKEN_COMMA,       /*!< , */
	PSS_COMP_LEX_TOKEN_EOF,          /*!< end of file */
	PSS_COMP_LEX_TOKEN_NUM_OF_ENTRIES /*!< the number of entries */
} pss_comp_lex_token_type_t;

/**
 * @brief the keyword enum
 **/
typedef enum {
	PSS_COMP_LEX_KEYWORD_ERROR = -1,
	PSS_COMP_LEX_KEYWORD_IN,
	PSS_COMP_LEX_KEYWORD_IF,          /* if */
	PSS_COMP_LEX_KEYWORD_ELSE,        /* else  */
	PSS_COMP_LEX_KEYWORD_FUNCTION,
	PSS_COMP_LEX_KEYWORD_VAR,
	PSS_COMP_LEX_KEYWORD_WHILE,
	PSS_COMP_LEX_KEYWORD_FOR,
	PSS_COMP_LEX_KEYWORD_BREAK,
	PSS_COMP_LEX_KEYWORD_CONTINUE,
	PSS_COMP_LEX_KEYWORD_RETURN,
	PSS_COMP_LEX_KEYWORD_UNDEFINED
} pss_comp_lex_keyword_t;

/**
 * @brief represents a token
 **/
typedef struct {
	const char* file;                       /*!< the file name of this token */
	uint32_t    line;                       /*!< the line number of this token in the source code */
	uint32_t    offset;                     /*!< the offset of this token in the source code*/
	pss_comp_lex_token_type_t type;         /*!< the type of this token */
	union {
		int32_t i;                          /*!< the integer value of this token */
		char s[4096];                       /*!< the string value of this token */
		const char* e;                      /*!< the error message */
		pss_comp_lex_keyword_t k;           /*!< the keyword data */
	} value;
} pss_comp_lex_token_t;

/**
 * @brief the incomplete type for a lexer
 **/
typedef struct _pss_comp_lex_t pss_comp_lex_t;

/**
 * @brief create a new plumber lexer
 * @param filename the filename used to display the error message
 * @param buffer The input buffer
 * @param size The size
 * @return the newly created lexer
 **/
pss_comp_lex_t* pss_comp_lex_new(const char* filename, const char* buffer, uint32_t size);

/**
 * @brief Get the filename of the lexter
 * @param lex The file name
 * @return the filename
 **/
const char* pss_comp_lex_get_filename(const pss_comp_lex_t* lex);

/**
 * @brief dispose a used lexer
 * @param lexer the target lexer
 * @return the status code
 **/
int pss_comp_lex_free(pss_comp_lex_t* lexer);

/**
 * @brief get the next token from the lexer
 * @param lexer the target lexer
 * @param buffer the token buffer
 * @return the status code
 **/
int pss_comp_lex_next_token(pss_comp_lex_t* lexer, pss_comp_lex_token_t* buffer);

#endif
