/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The PARSER Protocol Parser
 * @file http/parser/include/parser.h
 **/
#ifndef __PARSER_H__
#define __PARSER_H__

/**
 * @brief Represent a parsed string
 **/
typedef struct {
	char*         value;     /*!< The value of the string */
	size_t        length;    /*!< The length of the string */
} parser_string_t;

/**
 * @brief The method of the request
 **/
typedef enum {
	PARSER_METHOD_GET,   /*!< GET method */
	PARSER_METHOD_POST,  /*!< POST method */
	PARSER_METHOD_HEAD   /*!< HEAD method */
} parser_method_t;

/**
 * @brief The PARSER parser state
 **/
typedef struct {
	uint32_t             error:1;             /*!< If this is a invalid request */
	uint32_t             empty:1;             /*!< If the request don't have any data */
	uint32_t             keep_alive:1;        /*!< If the client ask to keep this connection */
	uint32_t             has_range:1;         /*!< Indicates if the request contains a range reuqest */
	parser_method_t      method;              /*!< The HTTP method */
	parser_string_t      path;                /*!< The path buffer (MAX: 2048 Bytes) */
	parser_string_t      host;                /*!< The host name buffer (MAX: 64 Bytes) */
	parser_string_t      query;               /*!< The query parameter (MAX: 2048 Bytes) */
	parser_string_t      accept_encoding;     /*!< The accept encoding buffer (MAX: 32 Bytes) */
	parser_string_t      body;                /*!< The body data (MAX: 2048) (TODO: we probably need a RLS token that wraps the pipe data directly) */
	parser_string_t      range_text;          /*!< The text for the range */
	uint64_t             range_begin;         /*!< The beginging of the range */
	uint64_t             range_end;           /*!< The end of the range */
	uint64_t             content_length;      /*!< The content length */
	uintpad_t __padding__[0];
	char                 internal_state[0];   /*!< The internal state */
} parser_state_t;

/**
 * @brief Create a new PARSER parser state
 * @return The newly created parser state
 **/
parser_state_t* parser_state_new(void);

/**
 * @brief Dispose a used parser state
 * @param state The parser state
 * @return status code
 **/
int parser_state_free(parser_state_t* state);

/**
 * @brief Process the next buffer of bytes
 * @param state The parser state variable
 * @param buf The buffer to process
 * @param sz The size of buffer
 * @return How many bytes that has been processed
 **/
size_t parser_process_next_buf(parser_state_t* state, const void* buf, size_t sz); 

/**
 * @brief Check if we just completed the parsing
 * @param state The state variable
 * @return status code
 **/
int parser_state_done(const parser_state_t* state);

#endif
