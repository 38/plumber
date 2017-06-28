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
 * @param result The statement result
 * @return status code
 **/
int pss_comp_stmt_parse(pss_comp_t* comp);

#endif
