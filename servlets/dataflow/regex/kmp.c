/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <pservlet.h>

#include <kmp.h>

/**
 * @brief The actual data structure for the KMP object
 **/
struct _kmp_pattern_t {
	size_t  size;    /*!< The size of the pattern */
	char*   pattern; /*!< The actual pattern data */
	size_t* prefix;  /*!< The prefix array */
};

kmp_pattern_t* kmp_pattern_new(const char* text, size_t len)
{
	if(NULL == text) ERROR_PTR_RETURN_LOG("Invalid arguments");

	kmp_pattern_t* ret = (kmp_pattern_t*)calloc(sizeof(*ret), 1);

	if(NULL == ret) 
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the KMP pattern object");

	if(NULL == (ret->pattern = (char*)malloc(len)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the pattern");

	memmove(ret->pattern, text, len);

	if(NULL == (ret->prefix = (size_t*)calloc(len, sizeof(size_t))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the prefix array");

	ret->prefix[0] = 0;

	size_t i;
	for(i = 1; i < len; i ++)
	{
		for(ret->prefix[i] = ret->prefix[i - 1] + 1; 
			ret->prefix[i] > 1 && 
			ret->pattern[ret->prefix[i] - 1] != ret->pattern[i];
			ret->prefix[i] = ret->prefix[ret->prefix[i] - 2] + 1);
		if(ret->prefix[i] == 1 && ret->pattern[0] != ret->pattern[i])
			ret->prefix[i] = 0;

	}

	ret->size = len;

	return ret;
ERR:

	if(NULL != ret)
	{
		if(NULL != ret->pattern) free(ret->pattern);
		if(NULL != ret->prefix) free(ret->prefix);
	}
	return NULL;
}

int kmp_pattern_free(kmp_pattern_t* kmp)
{
	if(NULL == kmp)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL != kmp->pattern) free(kmp->pattern);
	if(NULL != kmp->prefix) free(kmp->prefix);

	free(kmp);

	return 0;
}

/**
 * todo: Optimize this. This is not optimal and much slower than grep in a lot of cases. 
 *       What we need to do is use BM algorithm to get rid of the obviously unmatched string as early
 *       as possible. See misc/kmp.c for details
 **/
size_t kmp_partial_match(const kmp_pattern_t* kmp, const char* text, size_t maxlen, int eol_marker, size_t* state)
{
	if(NULL == kmp || NULL == text)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	size_t matched = state == NULL ? 0 : *state;
	size_t i;

	for(i = 0; i < maxlen && text[i] != eol_marker && matched < kmp->size; i ++)
	{
		for(;matched > 0 && text[i] != kmp->pattern[matched]; 
			 matched = kmp->prefix[matched - 1]);
		if(matched != 0 || (text[i] == kmp->pattern[0]))
			matched ++;
	}

	if(NULL != state) *state = matched;

	if(matched != kmp->size) 
		return i;

	return i - kmp->size;
}

size_t kmp_full_match(const kmp_pattern_t* kmp, const char* text, int eol_marker, size_t start, size_t len)
{
	if(NULL == kmp || NULL == text)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	size_t i;
	for(i = start; i < kmp->size && i < start + len && 
				   text[i - start] != eol_marker && 
				   text[i - start] == kmp->pattern[i]; i ++);

	if(len + start == i || text[i - start] == eol_marker)
		return i;

	return 0;
}

size_t kmp_pattern_length(const kmp_pattern_t* kmp)
{
	if(NULL == kmp) 
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	return kmp->size;
}
