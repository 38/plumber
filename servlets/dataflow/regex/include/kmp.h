/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The simple KMP string matcher
 * @file servelt/dataflow/regex/kmp.h
 **/
#ifndef __KMP_H__
#define __KMP_H__

/**
 * @brief A KMP matcher object
 **/
typedef struct _kmp_pattern_t kmp_pattern_t;

/**
 * @brief Create a new KMP object
 * @param pattern The pattern to search
 * @param len The size of the string
 * @return The KMP object
 **/
kmp_pattern_t* kmp_pattern_new(const char* pattern, size_t len);

/**
 * @brief Dispose a used KMP object
 * @param kmp the KMP object
 * @return status code
 **/
int kmp_pattern_free(kmp_pattern_t* kmp);

/**
 * @brief Match a KMP object
 * @param kmp The KMP object used for matching
 * @param text The text that we want to match
 * @param maxlen The maximum length of the string
 * @param eol_marker A char that indicates this is end of line, -1 indicates the matcher
 *                   should scan everything inside buffer
 * @param  state The state variabe. The state variable records how many bytes the KMP matcher has been matched
 *               until the function returns. It's used in multiple parts match and when the state is NULL, we
 *               just start over.
 * @return The location for the first match, or maxlen or position of eol_marker when no match found.
 *         error code for error cases. For all the cases the state variable will be stored.
 **/
size_t kmp_partial_match(const kmp_pattern_t* kmp, const char* text, size_t maxlen, int eol_marker, size_t* state);

/**
 * @brief Performe a full text match
 * @param kmp The KMP object
 * @param text The text object
 * @param eol_marker The marker for the End-Of-Line, this will interrupt the match
 * @param start The offset *in the pattern string* where we start matching
 * @param len The length of the text buffer
 * @return The size of matched string in this round (the next state varible). 0 indicates it's impossible to match. error code for
 *         all error cases
 **/
size_t kmp_full_match(const kmp_pattern_t* kmp, const char* text, int eol_marker, size_t start, size_t len);

/**
 * @brief Get the length of the pattern string
 * @param kmp The KMP object
 * @return the size
 **/
size_t kmp_pattern_length(const kmp_pattern_t* kmp);

#endif
