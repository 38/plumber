/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The PSS Virtual Machine runtime string
 * @file pss/include/pss/string.h
 **/
#ifndef __PSS_STRING_H__
#define __PSS_STRING_H__

/**
 * @brief Initialize the runtime value operations
 * @return status code
 **/
int pss_string_init();

/**
 * @biref Cleanup
 * @return status code
 **/
int pss_string_finalize();

/**
 * @brief Concatenate two string
 * @param left The first string
 * @param right The second string
 * @return The newly created concatenated string
 **/
char* pss_string_concat(const char* left, const char* right);

/**
 * @brief Get the literal representation of the string
 * @note This function will replace all the special chars with espace sequence and
 *       wrap the string with quote
 * @param str The string
 * @param buf The optional buffer, if NULL is given, the function will allocate a new buffer
 * @param sz  The size of the optional buffer, if NULL is given it will be ignored
 * @return The newly created string
 **/
char* pss_string_literal(const char* str, char* buf, size_t sz);

#endif
