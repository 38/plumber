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
 * @brief Parse one statement
 * @param comp The compiler
 * @param valbuf The value buffer, only used when the statement is an expression statement
 *               If this is passed, the result of the expression won't be discard and it will
 *               be passed back.
 *               If NULL is given, means we want to discard the result right away
 * @return status code
 **/
int pss_comp_stmt_parse(pss_comp_t* comp, pss_comp_value_t* valbuf);

#endif
