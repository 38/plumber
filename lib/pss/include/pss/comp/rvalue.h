/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The RValue parser
 * @pfile pss/comp/rvalue.h
 **/
#ifndef __PSS_COMP_RVALUE_H__
#define __PSS_COMP_RVALUE_H__

/**
 * @brief The rvalue compilation result
 **/
typedef struct {
	pss_bytecode_regid_t  result;   /*!< The tmp that holds the result of r-value */
} pss_comp_rvalue_t;

/**
 * @brief Parse the rvalue
 * @param comp The compilter instance
 * @param allow_dict If the Rvalue allows a dictionary literal
 * @param result The compilation result
 * @return s tatus code
 **/
int pss_comp_rvalue_parse(pss_comp_t* comp, int allow_dict, pss_comp_rvalue_t* result);

#endif
