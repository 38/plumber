/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The block parser, a block means everything inside a scope
 * @file  include/pss/comp/block.h
 **/
#ifndef __PSS_COMP_BLOCK_H__
#define __PSS_COMP_BLOCK_H__

/**
 * @brief The compilation result
 **/
typedef struct {
	pss_bytecode_addr_t begin;   /*!< The address of the first bytecode for this block */
	pss_bytecode_addr_t end;     /*!< The address after the last bytecode for this block */
} pss_comp_block_t;

/**
 * @brief Parse a code block ends with the lexer token "last_token"
 * @param comp The compiler instance
 * @param first_token The expected first token, If there's no leading token, pass PSS_COMP_LEX_TOKEN_NAT
 * @param last_token The last token
 * @param result The block compilation result buffer
 * @return status code, if there's an error, the error status will be set
 **/
int pss_comp_block_parse(pss_comp_t* comp, pss_comp_lex_token_type_t first_token, pss_comp_lex_token_type_t last_token, pss_comp_block_t* result);

#endif /* __PSS_COMP_BLOCK_H__ */
