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
   
#if PCRE_MAJOR >= 8 && PCRE_MINOR >= 32 && 0
/* TODO: figure out if we need to use the pcre_jit_exec, but it now cause crash */
#	define PCRE_EXEC(args...) pcre_jit_exec(args, NULL)
#else
#	define PCRE_EXEC pcre_exec
#endif

#ifdef PCRE_CONFIG_JIT
#	define PCRE_FREE_STUDY pcre_free_study
#else
#	define PCRE_FREE_STUDY pcre_free
#endif

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

	if(NULL == (ret->extra = pcre_study(ret->regex, 0, &pcre_err_msg)) && pcre_err_msg != NULL)
		ERROR_LOG_GOTO(ERR, "Cannot optimize the regular expression: %s", pcre_err_msg);

	return ret;

ERR:
	if(NULL != ret->extra) PCRE_FREE_STUDY(ret->extra);
	if(NULL != ret->regex) pcre_free(ret->regex);

	free(ret);

	return NULL;
}

int re_free(re_t* obj)
{
	if(NULL == obj) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(obj->extra != NULL) PCRE_FREE_STUDY(obj->extra);
	if(NULL != obj->regex) pcre_free(obj->regex);

	free(obj);

	return 0;
}

static inline int _process_pcre_error_code(const char* msg, int code)
{
	switch(code)
	{
		case PCRE_ERROR_NOMATCH:
			return 0;
		case PCRE_ERROR_NULL:
			ERROR_RETURN_LOG(int, "%s: Invalid arguments", msg);
		case PCRE_ERROR_BADOPTION:
			ERROR_RETURN_LOG(int, "%s: Bad options", msg);
		case PCRE_ERROR_BADMAGIC:
			ERROR_RETURN_LOG(int, "%s: Invalid regex object", msg);
		case PCRE_ERROR_NOMEMORY:
			ERROR_RETURN_LOG(int, "%s: Out of meomry", msg);
		default:
			ERROR_RETURN_LOG(int, "%s: generic error", msg);
	}
}

int re_match_full(re_t* obj, const char* text, size_t len)
{
	if(NULL == obj || NULL == obj->regex || NULL == text)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	int ovec[30];

	int match_rc = PCRE_EXEC(obj->regex, obj->extra, text, (int)len, 0, 0, ovec, sizeof(ovec) / sizeof(ovec[0]));

	if(match_rc >= 0) return 1;

	return _process_pcre_error_code("pcre_exec", match_rc);
}

int re_match_partial(re_t* obj, const char* text, size_t len)
{
	if(NULL == obj || NULL == obj->regex || NULL == text)
		ERROR_RETURN_LOG(int, "Invalid arguments");
	
	int ovec[30];

	int match_rc = PCRE_EXEC(obj->regex, obj->extra, text, (int)len, 0, PCRE_PARTIAL_SOFT, ovec, sizeof(ovec) / sizeof(ovec[0]));

	if(match_rc >= 0)  return 1;

	return _process_pcre_error_code("pcre_exec", match_rc);
}

