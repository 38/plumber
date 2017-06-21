/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The statement parser
 * @file  include/pss/comp/stmt.h
 **/

#ifndef __PSS_COMP_STMT_H__
#define __PSS_COMP_STMT_H__

/**
 * @brief The compiler result for a statement 
 **/
typedef struct {
	pss_bytecode_addr_t begin;   /*!< The address of first instruction for this statement */
	pss_bytecode_addr_t end;     /*!< The address after the last instruction for this statement */
} pss_comp_stmt_result_t;

/**
 * @brief Parse one statement
 * @param comp The compiler
 * @param result The statement result
 * @return status code
 **/
int pss_comp_stmt_parse(pss_comp_t* comp, pss_comp_stmt_result_t* result); 

#endif
