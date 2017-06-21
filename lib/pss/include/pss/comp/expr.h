/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The expression parser for the PSS
 * @file pss/comp/expr.h
 **/
#ifndef __PSS_COMP_EXPR_H__
#define __PSS_COMP_EXPR_H__

/**
 * @brief Parse an expression 
 * @param comp The compiler instance
 * @param buf The result buffer
 * @return status code
 **/
int pss_comp_expr_parse(pss_comp_t* comp, pss_comp_value_t* buf);

#endif
