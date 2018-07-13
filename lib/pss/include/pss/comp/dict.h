/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The dictionary / service graph literal parser
 * @file include/pss/comp/dict.h
 **/
#ifndef __PSS_COMP_DICT_H__
#define __PSS_COMP_DICT_H__

/**
 * @brief Parse the dictionary or service-graph literal
 * @param comp The compiler instance
 * @param buf  The result buffer
 * @return status code
 **/
int pss_comp_dict_parse(pss_comp_t* comp, pss_comp_value_t* buf);

/**
 * @brief Parse the list literal
 * @param comp The compiler instance
 * @param buf  The result buffer
 * @return status code
 **/
int pss_comp_dict_parse_list(pss_comp_t* comp, pss_comp_value_t* buf);

#endif
