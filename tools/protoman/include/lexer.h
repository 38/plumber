/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The protocol type language lexer
 * @file protoman/include/lexer.h
 **/
#ifndef __LEXER_H__
#define __LEXER_H__

/**
 * @brief the lexer token type
 **/
typedef enum {
	LEXER_TOKEN_EOF,              /*!< the token indicates this is the end of the file */
	LEXER_TOKEN_ID,               /*!< an identifier */
	LEXER_TOKEN_NUMBER,           /*!< the number */
	LEXER_TOKEN_FLOAT_POINT,      /*!< A float point number */
	LEXER_TOKEN_DOT,              /*!< dot */
	LEXER_TOKEN_COLON,            /*!< : */
	LEXER_TOKEN_SEMICOLON,        /*!< ; */
	LEXER_TOKEN_COMMA,            /*!< , */
	LEXER_TOKEN_LBRACE,           /*!< { */
	LEXER_TOKEN_RBRACE,           /*!< } */
	LEXER_TOKEN_LBRACKET,         /*!< [ */
	LEXER_TOKEN_RBRACKET,         /*!< ] */
	LEXER_TOKEN_EQUAL,            /*!< = */
	LEXER_TOKEN_AT,               /*!< @ */
	LEXER_TOKEN_K_TYPE,           /*!< the keyword "type" */
	LEXER_TOKEN_K_ALIAS,          /*!< the keyword "alias" */
	LEXER_TOKEN_K_PACKAGE,        /*!< the keyword "package" */
	LEXER_TOKEN_TYPE_PRIMITIVE    /*!< the type primitives */
} lexer_token_type_t;

/**
 * @brief the data structure for a lexer token
 **/
typedef struct {
	lexer_token_type_t type;     /*!< the token type */
	const char*        file;     /*!< the filename which contains the token */
	uint32_t           line;     /*!< the line number for this token */
	uint32_t           column;   /*!< the column number for this token */
	proto_type_atomic_metadata_t metadata; /*!< The type metadata carried by this lexer token */
	uintptr_t __padding__[0];
	union {
		int64_t  number;        /*!< the number value */
		double   floatpoint;    /*!< The float point number */
		uint32_t size;          /*!< the size of this type primitive */
		char     id[0];         /*!< the identifer */
	} data[0];                  /*!< the pointer to the token data */
} lexer_token_t;

/**
 * @brief the incomplete type for the protocol type language lexer
 **/
typedef struct _lexer_t lexer_t;

/**
 * @brief create a new lexer that reads the given filename
 * @param filename the filename to read
 * @return the newely created lexer, or NULL on error case
 **/
lexer_t* lexer_new(const char* filename);

/**
 * @brief dispose a used lexer object
 * @param lexer the lexer to be disposed
 * @return the status code
 **/
int lexer_free(lexer_t* lexer);

/**
 * @brief get the next token from the stream
 * @param lexer the lexer we use to get next token
 * @return the next token
 * @note this function will create a new object and the caller should call ptype_lexer_token_free after
 *       the token is no longer used
 **/
lexer_token_t* lexer_next_token(lexer_t* lexer);

/**
 * @brief dispose a used lexer token
 * @param token the token to dispose
 * @return the status code
 **/
int lexer_token_free(lexer_token_t* token);

#endif /* __PROTOMAN_LEXER_H__ */
