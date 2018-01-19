/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include <re2/re2.h>
#include <re.h>

#include <error.h>

#include <pservlet.h>

extern "C" {
	/**
	 * @brief The actual data structure for the regex 
	 **/
	struct _re_t {
		RE2* regex;  /*!< The internal regex function */
	};

	re_t* re_new(const char* regex)
	{
		if(NULL == regex) ERROR_PTR_RETURN_LOG("Invalid arguments");

		re_t* ret = (re_t*)malloc(sizeof(_re_t));

		if(NULL == ret) 
			ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the regular expression object");

		ret->regex = new RE2(regex);

		if(!ret->regex->ok())
		{
			delete ret->regex;
			free(ret);
			ERROR_PTR_RETURN_LOG("Cannot compile regular expression");
		}

		return ret;
	}

	int re_free(re_t* obj)
	{
		if(NULL == obj) ERROR_RETURN_LOG(int, "Invalid arguments");

		if(obj->regex != NULL) 
			delete obj->regex;

		free(obj);

		return 0;
	}

	int re_match(re_t* obj, const char* text, size_t len)
	{
		if(NULL == obj || NULL == obj->regex || NULL == text)
			ERROR_RETURN_LOG(int, "Invalid arguments");

		re2::StringPiece text_obj(text, (int)len);

		if(RE2::FullMatch(text_obj, *obj->regex))
			return 1;
		else
			return 0;
	}

}

