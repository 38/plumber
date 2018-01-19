/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The wrapper for the libre2 library in C
 * @file servelt/dataflow/regex/include/re.h
 **/
#ifndef __RE_H__
#define __RE_H__
/**
 * @brief The regular expression object
 **/
typedef struct _re_t re_t;

/**
 * @brief Compile the regular expression
 * @param regex The regular expression
 * @return The newly created regular expression object
 **/
re_t* re_new(const char* regex);

/**
 * @brief Dispose a used regex
 * @param obj The regular expression object
 * @return status code
 **/
int re_free(re_t* obj);

/**
 * @brief Fully match a regular expression
 * @param obj The object
 * @param str The input string
 * @param len The len of the string buffer
 * @return 1 for fully matched. 0 for unmatched. error code on error
 **/
int re_match_full(re_t* obj, const char* text, size_t len);

/**
 * @brief Partially match a regular expression
 * @param obj The object
 * @param str The input string
 * @param len The len of the string buffer
 * @return 1 for fully matched. 0 for unmatched. error code on error
 **/
int re_match_partial(re_t* obj, const char* text, size_t len);

#endif /* __RE_H__ */
