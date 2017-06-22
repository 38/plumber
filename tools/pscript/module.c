/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#include <constants.h>
#include <error.h>

#include <pss.h>
#include <module.h>

static char const* const* _search_paths;

int module_set_search_path(char const* const* path)
{
	if(NULL == path) ERROR_RETURN_LOG(int, "Invalid arguments");

	_search_paths = path;

	return 0;
}

time_t _get_file_ts(const char* path, off_t* size)
{
	struct stat st;
	if(stat(path, &st) < 0) return ERROR_CODE(time_t);

	if(NULL != size) *size = st.st_size;

	return st.st_ctime;
}

pss_bytecode_module_t* module_from_file(const char* name, int dump_compiled)
{
	if(NULL == name) ERROR_PTR_RETURN_LOG("Invalid arguments");

	uint32_t i;
	for(i = 0; _search_paths != NULL && _search_paths[i] != NULL; i ++)
	{
		char module_name[PATH_MAX];
		char source_path[PATH_MAX];
		char compiled_path[PATH_MAX];
		size_t len = strlen(name);
		if(len > 4 && 0 == strcmp(name + len - 4, ".pss"))
			len -= 4;
		memcpy(module_name, name, len);
		module_name[len] = 0;
		snprintf(source_path, sizeof(source_path), "%s/%s.pss", _search_paths[i], module_name);
		snprintf(compiled_path, sizeof(compiled_path), "%s/%s.psm", _search_paths[i], module_name);

		off_t source_sz;
		time_t source_ts   = _get_file_ts(source_path, &source_sz);
		time_t compiled_ts = _get_file_ts(compiled_path, NULL);

		if(ERROR_CODE(time_t) != compiled_ts && (source_ts == ERROR_CODE(time_t) || compiled_ts > source_ts))
		{
			LOG_DEBUG("Found compiled PSS module at %s", compiled_path);
			return pss_bytecode_module_load(compiled_path);
		}
		else if(ERROR_CODE(time_t) != source_ts)
		{
			LOG_DEBUG("Found PSS module source at %s", source_path);
			char* code = (char*)malloc((size_t)(source_sz + 1));
			pss_comp_lex_t* lexer = NULL;
			pss_bytecode_module_t* module = NULL;
			pss_comp_error_t* err = NULL;
			code[source_sz] = 0;

			FILE* fp = fopen(source_path, "r");
			if(NULL == fp)
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the source code file");

			if(fread(code, (size_t)source_sz, 1, fp) != 1)
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read file");

			if(fclose(fp) < 0)
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot close file");
			else 
				fp = NULL;

			if(NULL == (lexer = pss_comp_lex_new(source_path, code, (unsigned)source_sz + 1)))
				ERROR_LOG_GOTO(ERR, "Cannot create lexer");

			if(NULL == (module = pss_bytecode_module_new()))
				ERROR_LOG_GOTO(ERR, "Cannot create module instance");

			pss_comp_option_t opt = {
				.lexer = lexer,
				.module = module
			};

			if(ERROR_CODE(int) == pss_comp_compile(&opt, &err))
				ERROR_LOG_GOTO(ERR, "Cannot compile the source code");

			if(ERROR_CODE(int) == pss_comp_lex_free(lexer))
				LOG_WARNING("Cannot dispose the lexer instance");
			free(code);

			if(dump_compiled)
			{
				if(ERROR_CODE(int) == pss_bytecode_module_dump(module, compiled_path))
					LOG_WARNING("Cannot dump the compiled module to file");
			}

			return module;
ERR:
			if(NULL != fp) fclose(fp);
			if(NULL != code) free(code);
			if(NULL != lexer) pss_comp_lex_free(lexer);
			if(NULL != module) pss_bytecode_module_free(module);

			if(NULL != err)
			{
				const pss_comp_error_t* this;
				for(this = err; NULL != this; this = this->next)
					fprintf(stderr, "%s:%u:%u:error: %s", this->filename, this->line + 1, this->column + 1, this->message);
				pss_comp_free_error(err);
			}
			return NULL;
		}

	}
	ERROR_PTR_RETURN_LOG("Cannot found the script");
}
