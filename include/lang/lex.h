/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The lexer of SLD
 * @file lang/lex.h
 **/
#ifndef __PLUMBER_LANG_LEX_H__
#define __PLUMBER_LANG_LEX_H__

/**
 * @brief the token types of the lexer
 **/
typedef enum {
	LANG_LEX_TOKEN_NAT = -2,    /*!< Not A Token */
	LANG_LEX_TOKEN_ERROR = -1,  /*!< means a lexical error */
	LANG_LEX_TOKEN_KEYWORD,     /*!< a keyword */
	LANG_LEX_TOKEN_IDENTIFIER,  /*!< a identifier */
	LANG_LEX_TOKEN_INTEGER,     /*!< a integer literal */
	LANG_LEX_TOKEN_STRING,      /*!< a string literal */
	LANG_LEX_TOKEN_EQUAL,       /*!< = */
	LANG_LEX_TOKEN_COLON_EQUAL, /*!< := */
	LANG_LEX_TOKEN_LPARENTHESIS,/*!< ( */
	LANG_LEX_TOKEN_RPARENTHESIS,/*!< ) */
	LANG_LEX_TOKEN_GRAPHVIZ_PROP, /*!< a graphviz property [....] */
	LANG_LEX_TOKEN_LBRACE,      /*!< { */
	LANG_LEX_TOKEN_RBRACE,      /*!< } */
	LANG_LEX_TOKEN_LT,          /*!< &lt; */
	LANG_LEX_TOKEN_LE,          /*!< &lt;= */
	LANG_LEX_TOKEN_GT,          /*!< &gt; */
	LANG_LEX_TOKEN_TRIPLE_GT,   /*!< &gt;&gt;&gt */
	LANG_LEX_TOKEN_GE,          /*!< &gt;= */
	LANG_LEX_TOKEN_EQUALEQUAL,  /*!< == */
	LANG_LEX_TOKEN_NE,          /*!< != */
	LANG_LEX_TOKEN_NOT,         /*!< ! */
	LANG_LEX_TOKEN_AND,         /*!< && */
	LANG_LEX_TOKEN_OR,          /*!< || */
	LANG_LEX_TOKEN_ARROW,       /*!< -&gt; */
	LANG_LEX_TOKEN_SEMICOLON,   /*!< ; */
	LANG_LEX_TOKEN_DOT,         /*!< dot */
	LANG_LEX_TOKEN_ADD,         /*!< + */
	LANG_LEX_TOKEN_ADD_EQUAL,   /*!< += */
	LANG_LEX_TOKEN_INCREASE,    /*!< ++ */
	LANG_LEX_TOKEN_MINUS,       /*!< - */
	LANG_LEX_TOKEN_MINUS_EQUAL, /*!< -= */
	LANG_LEX_TOKEN_DECREASE,    /*!< -- */
	LANG_LEX_TOKEN_TIMES,       /*!< * */
	LANG_LEX_TOKEN_TIMES_EQUAL, /*!< *= */
	LANG_LEX_TOKEN_DIVIDE,      /*!< / */
	LANG_LEX_TOKEN_DIVIDE_EQUAL,/*!< /= */
	LANG_LEX_TOKEN_MODULAR,     /*!< % */
	LANG_LEX_TOKEN_MODULAR_EQUAL, /*!< %= */
	LANG_LEX_TOKEN_COMMA,       /*!< , */
	LANG_LEX_TOKEN_EOF,          /*!< end of file */
	LANG_LEX_TOKEN_NUM_OF_ENTRIES /*!< the number of entries */
} lang_lex_token_type_t;

/**
 * @brief the keyword enum
 **/
typedef enum {
	LANG_LEX_KEYWORD_ERROR = -1,
	LANG_LEX_KEYWORD_ECHO,
	LANG_LEX_KEYWORD_VISUALIZE,
	LANG_LEX_KEYWORD_START,
	LANG_LEX_KEYWORD_INCLUDE,
	LANG_LEX_KEYWORD_INSMOD,
	LANG_LEX_KEYWORD_IF,          /* if */
	LANG_LEX_KEYWORD_ELSE,        /* else  */
	LANG_LEX_KEYWORD_VAR,
	LANG_LEX_KEYWORD_WHILE,
	LANG_LEX_KEYWORD_FOR,
	LANG_LEX_KEYWORD_BREAK,
	LANG_LEX_KEYWORD_CONTINUE,
	LANG_LEX_KEYWORD_UNDEFINED
} lang_lex_keyword_t;

/**
 * @brief represents a token
 **/
typedef struct {
	const char* file;                       /*!< the file name of this token */
	uint32_t    line;                       /*!< the line number of this token in the source code */
	uint32_t    offset;                     /*!< the offset of this token in the source code*/
	lang_lex_token_type_t type;    /*!< the type of this token */
	union {
		int32_t i;                          /*!< the integer value of this token */
		char s[4096];                       /*!< the string value of this token */
		const char* e;                      /*!< the error message */
		lang_lex_keyword_t k;               /*!< the keyword data */
	} value;
} lang_lex_token_t;

/**
 * @brief the incomplete type for a lexer
 **/
typedef struct _lang_lex_t lang_lex_t;

/**
 * @brief initialize this file
 * @return status code
 **/
int lang_lex_init();

/**
 * @brief finalize this file
 * @return status code
 **/
int lang_lex_finalize();

/**
 * @brief add a new directory to the SDL script serch path
 * @param path the script search path
 * return the status code
 **/
int lang_lex_add_script_search_path(const char* path);

/**
 * @brief clear the script search path
 * @return the status code
 **/
int lang_lex_clear_script_search_path();

/**
 * @brief get the script search path list
 * @return a pointer to the search path list
 **/
char const* const* lang_lex_get_script_search_paths();

/**
 * @brief get the length of the script search path
 * @return the number of the search path list or error code
 **/
size_t lang_lex_get_num_script_search_paths();

/**
 * @brief create a new plumber lexer
 * @param filename the filename used to display the error message
 * @return the newly created lexer
 **/
lang_lex_t* lang_lex_new(const char* filename);

/**
 * @brief dispose a used lexer
 * @param lexer the target lexer
 * @return the status code
 **/
int lang_lex_free(lang_lex_t* lexer);

/**
 * @brief get the next token from the lexer
 * @param lexer the target lexer
 * @param buffer the token buffer
 * @return the status code
 **/
int lang_lex_next_token(lang_lex_t* lexer, lang_lex_token_t* buffer);

/**
 * @brief include a script
 * @param lexer the top-level lexer
 * @param filename the target filename to include
 * @return status code
 **/
int lang_lex_include_script(lang_lex_t* lexer, const char* filename);

/**
 * @brief pop a include file
 * @param lexer the target lexer
 * @return status code
 **/
int lang_lex_pop_include_script(lang_lex_t* lexer);

/**
 * @brief create a lexer object from a memory buffer
 * @param buffer the memory buffer used to create
 * @param size the size of the buffer
 * @return the newly create lexer, or NULL
 **/
lang_lex_t* lang_lex_from_buffer(char* buffer, uint32_t size);


#endif /* __PLUMBER_LANG_LEX_H__ */
