/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include <pcre.h>

#include <error.h>

#include <pservlet.h>

#include <re.h>

/**
 * @brief The actual data structure for the regex 
 **/
struct _re_t {
	pcre*         regex;   /*!< The PCRE object */
	pcre_extra*   extra;   /*!< The PCRE extra data */
};

re_t* re_new(const char* regex)
{
	if(NULL == regex) ERROR_PTR_RETURN_LOG("Invalid arguments");

	re_t* ret = (re_t*)calloc(sizeof(re_t), 1);

	if(NULL == ret) 
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the regular expression object");

	const char* pcre_err_msg = "";
	int pcre_err_ofs = 0;


	if(NULL == (ret->regex = pcre_compile(regex, 0, &pcre_err_msg, &pcre_err_ofs, NULL)))
		ERROR_LOG_GOTO(ERR, "Cannot compile regular expression: %s at %d", pcre_err_msg, pcre_err_ofs);

	if(NULL == (ret->extra = pcre_study(ret->regex, 0, &pcre_err_msg)))
		ERROR_LOG_GOTO(ERR, "Cannot optimize the regular expression: %s", pcre_err_msg);

	return ret;

ERR:
	if(NULL != ret->extra) 
#ifdef PCRE_CONFIG_JIT
		pcre_free_study(ret->extra);
#else
		pcre_free(ret->extra);
#endif
	if(NULL != ret->regex) pcre_free(ret->regex);

	free(ret);

	return NULL;
}

int re_free(re_t* obj)
{
	if(NULL == obj) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(obj->extra != NULL)
#ifdef PCRE_CONFIG_JIT
		pcre_free_study(obj->extra);
#else
		pcre_free(obj->extra);
#endif

	if(NULL != obj->regex) 
		pcre_free(obj->regex);

	free(obj);

	return 0;
}


   
#if PCRE_MAJOR >= 8 && PCRE_MINOR >= 32
#	define PCRE_EXEC(args...) pcre_jit_exec(args, NULL)
#else
#	define PCRE_EXEC pcre_exec
#endif

int re_match_full(re_t* obj, const char* text, size_t len)
{
	if(NULL == obj || NULL == obj->regex || NULL == text)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	int ovec[30];

	int match_rc = PCRE_EXEC(obj->regex, obj->extra, text, (int)len, 0, 0, ovec, sizeof(ovec) / sizeof(ovec[0]));

	switch(match_rc)
	{
		case PCRE_ERROR_NOMATCH:
			return 0;
		case PCRE_ERROR_NULL:
			ERROR_RETURN_LOG(int, "pcre_exec: Invalid arguments");
		case PCRE_ERROR_BADOPTION:
			ERROR_RETURN_LOG(int, "pcre_exec: Bad options");
		case PCRE_ERROR_BADMAGIC:
			ERROR_RETURN_LOG(int, "pcre_exec: Invalid regex object");
		case PCRE_ERROR_NOMEMORY:
			ERROR_RETURN_LOG(int, "pcre_exec: Out of meomry");
		default:
			ERROR_RETURN_LOG(int, "pcre_exec: generic error");
	}
}

int re_match_partial(re_t* obj, const char* text, size_t len)
{
	if(NULL == obj || NULL == obj->regex || NULL == text)
		ERROR_RETURN_LOG(int, "Invalid arguments");
	
	int ovec[30];

	int match_rc = PCRE_EXEC(obj->regex, obj->extra, text, (int)len, 0, PCRE_PARTIAL_SOFT, ovec, sizeof(ovec) / sizeof(ovec[0]));

	switch(match_rc)
	{
		case PCRE_ERROR_NOMATCH:
			return 0;
		case PCRE_ERROR_NULL:
			ERROR_RETURN_LOG(int, "pcre_exec: Invalid arguments");
		case PCRE_ERROR_BADOPTION:
			ERROR_RETURN_LOG(int, "pcre_exec: Bad options");
		case PCRE_ERROR_BADMAGIC:
			ERROR_RETURN_LOG(int, "pcre_exec: Invalid regex object");
		case PCRE_ERROR_NOMEMORY:
			ERROR_RETURN_LOG(int, "pcre_exec: Out of meomry");
		default:
			ERROR_RETURN_LOG(int, "pcre_exec: generic error");
	}

	return 0;
}

