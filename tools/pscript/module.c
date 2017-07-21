/**
 * Copyright (C) 2017, Hao Hou
 * Copyright (C) 2017, Feng Liu
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

#include <utils/hash/murmurhash3.h>

#include <pss.h>
#include <module.h>

typedef struct _loaded_module_t {
	uint64_t hash[2];
	pss_bytecode_module_t* module;
	struct _loaded_module_t* next;
} _loaded_module_t;

static _loaded_module_t* _modules = NULL;

static char const* const* _search_path = NULL;


int module_set_search_path(char const* const* paths)
{
	if(NULL == paths) ERROR_RETURN_LOG(int, "Invalid arguments");
	_search_path = paths;
	return 0;
}


time_t _get_file_ts(const char* path, off_t* size)
{
	struct stat st;
	if(stat(path, &st) < 0) return ERROR_CODE(time_t);

	if(NULL != size) *size = st.st_size;

	return st.st_ctime;
}

static inline void _add_module_to_list(pss_bytecode_module_t* m, uint64_t hash[2])
{
	_loaded_module_t* module = (_loaded_module_t*)malloc(sizeof(*module));
	if(NULL == module)
	{
		LOG_ERROR_ERRNO("Cannot allocate memory for the new module node");
		return;
	}
	module->hash[0] = hash[0];
	module->hash[1] = hash[1];
	module->module = m;
	module->next = _modules;
	_modules = module;
}

static pss_bytecode_module_t* _try_load_module_from_buffer(const char* code, uint32_t code_size, uint32_t debug)
{
	pss_comp_lex_t* lexer = NULL;
	pss_bytecode_module_t* module = NULL;
	pss_comp_error_t* err = NULL;

	if(NULL == (lexer = pss_comp_lex_new("stdin", code, code_size)))
	    ERROR_LOG_GOTO(_ERR, "Cannot create lexer");
	if(NULL == (module = pss_bytecode_module_new()))
	    ERROR_LOG_GOTO(_ERR, "Cannot create module instance");
	pss_comp_option_t opt = {
		.lexer = lexer,
		.module = module,
		.debug = (debug != 0)
	};
	if(ERROR_CODE(int) == pss_comp_compile(&opt, &err))
	    ERROR_LOG_GOTO(_ERR, "Can not compile the source code");

	if(ERROR_CODE(int) == pss_comp_lex_free(lexer))
	    LOG_WARNING("Cannot dispose the lexer instance");
	return module;
_ERR:
	if(NULL != err)
	{
		const pss_comp_error_t* this;
		for(this = err; NULL != this; this = this->next)
		    fprintf(stderr, "%s:%u:%u:error: %s\n", this->filename, this->line + 1, this->column + 1, this->message);
		pss_comp_free_error(err);
	}

	if(NULL != lexer) pss_comp_lex_free(lexer);
	if(NULL != module) pss_bytecode_module_free(module);
	return NULL;
}

pss_bytecode_module_t* module_from_buffer(const char* code, uint32_t code_size, uint32_t debug)
{
	pss_bytecode_module_t* module = NULL;
	if(NULL == (module = _try_load_module_from_buffer(code, code_size, debug)))
	    return NULL;
	uint64_t hash[2] = {0, 0};
	_add_module_to_list(module, hash);
	return module;
}


int _try_load_module(const char* source_path, const char* compiled_path, int load_compiled, int dump_compiled, int debug, pss_bytecode_module_t** ret)
{
	off_t source_sz = 0;
	time_t source_ts   = source_path == NULL ? ERROR_CODE(time_t) : _get_file_ts(source_path, &source_sz);
	time_t compiled_ts = compiled_path == NULL ? ERROR_CODE(time_t) : _get_file_ts(compiled_path, NULL);

	if(load_compiled && ERROR_CODE(time_t) != compiled_ts && (source_ts == ERROR_CODE(time_t) || compiled_ts > source_ts))
	{
		LOG_DEBUG("Found compiled PSS module at %s", compiled_path);
		if(NULL == (*ret = pss_bytecode_module_load(compiled_path)))
		    ERROR_RETURN_LOG(int, "Cannot alod module from file %s", compiled_path);
		return 1;
	}
	else if(ERROR_CODE(time_t) != source_ts)
	{
		LOG_DEBUG("Found PSS module source at %s", source_path);
		pss_bytecode_module_t* module = NULL;
		char* code = (char*)malloc((size_t)(source_sz + 1));
		if(NULL == code)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for code");
		code[source_sz] = 0;

		FILE* fp = fopen(source_path, "r");
		if(NULL == fp)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open the source code file");

		if(source_sz != 0 && fread(code, (size_t)source_sz, 1, fp) != 1)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read file");

		if(fclose(fp) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot close file");
		else
		    fp = NULL;

		if(NULL == (module = _try_load_module_from_buffer(code, (unsigned)(source_sz + 1), (uint32_t)debug)))
		    goto ERR;

		free(code);

		if(compiled_path != NULL && dump_compiled && ERROR_CODE(int) == pss_bytecode_module_dump(module, compiled_path))
		    LOG_WARNING("Cannot dump the compiled module to file");

		*ret = module;

		return 1;
ERR:
		if(NULL != fp) fclose(fp);
		if(NULL != code) free(code);

		return ERROR_CODE(int);
	}

	return 0;
}

static inline void _compute_hash(const char* str, uint64_t* hash)
{
	size_t len = strlen(str);
	murmurhash3_128(str, len, 0x1234567u, hash);
}

static inline int _is_previously_loaded(const char* source_path)
{
	uint64_t hash[2];

	_compute_hash(source_path, hash);

	const _loaded_module_t* ptr;
	for(ptr = _modules; NULL != ptr && (ptr->hash[0] != hash[0] || ptr->hash[1] != hash[1]); ptr = ptr->next);

	return (ptr != NULL);
}

pss_bytecode_module_t* module_from_file(const char* name, int load_compiled, int dump_compiled, int debug, const char* compiled_output)
{
	if(NULL == name) ERROR_PTR_RETURN_LOG("Invalid arguments");

	uint32_t i;
	pss_bytecode_module_t* ret;
	uint64_t hash[2] = {};
	for(i = 0; _search_path != NULL && _search_path[i] != NULL; i ++)
	{
		char module_name[PATH_MAX];
		char source_path[PATH_MAX];
		char compiled_path[PATH_MAX];
		size_t len = strlen(name);
		if(len > 4 && 0 == strcmp(name + len - 4, ".pss"))
		    len -= 4;
		else
		{
			/* Try to load the fullpath and do not dump the psm if the filename is not end with pss */
			snprintf(source_path, sizeof(source_path), "%s/%s", _search_path[i], name);

			_compute_hash(source_path, hash);
			int rc = _try_load_module(source_path, NULL, 0, 0, debug, &ret);
			if(ERROR_CODE(int) == rc) ERROR_PTR_RETURN_LOG("Cannot load module");

			if(rc == 1) goto FOUND;
		}

		/* If not found, then we need try to find the compiled */
		memcpy(module_name, name, len);
		module_name[len] = 0;
		snprintf(source_path, sizeof(source_path), "%s/%s.pss", _search_path[i], module_name);
		if(compiled_output == NULL)
		    snprintf(compiled_path, sizeof(compiled_path), "%s/%s.psm", _search_path[i], module_name);
		else
		    snprintf(compiled_path, sizeof(compiled_path), "%s", compiled_output);

		_compute_hash(source_path, hash);
		int rc = _try_load_module(source_path, compiled_path, load_compiled, dump_compiled, debug, &ret);
		if(ERROR_CODE(int) == rc) ERROR_PTR_RETURN_LOG("Cannot load module");

		if(rc == 1) goto FOUND;
	}
	ERROR_PTR_RETURN_LOG("Cannot found the script");
FOUND:
	_add_module_to_list(ret, hash);
	return ret;
}

int module_is_loaded(const char* name)
{
	if(NULL == name) ERROR_RETURN_LOG(int, "Invalid arguments");
	uint32_t i;
	for(i = 0; _search_path != NULL && _search_path[i] != NULL; i ++)
	{
		char module_name[PATH_MAX];
		char source_path[PATH_MAX];
		size_t len = strlen(name);
		if(len > 4 && 0 == strcmp(name + len - 4, ".pss"))
		    len -= 4;
		else
		{
			/* Try to load the fullpath and do not dump the psm if the filename is not end with pss */
			snprintf(source_path, sizeof(source_path), "%s/%s", _search_path[i], name);
			if(_is_previously_loaded(source_path)) return 1;
		}

		memcpy(module_name, name, len);
		module_name[len] = 0;
		snprintf(source_path, sizeof(source_path), "%s/%s.pss", _search_path[i], module_name);
		if(_is_previously_loaded(source_path)) return 1;
	}

	return 0;
}

int module_unload_all()
{
	int rc = 0;
	_loaded_module_t* ptr;
	for(ptr = _modules; ptr != NULL;)
	{
		_loaded_module_t* this = ptr;
		ptr = ptr->next;

		if(ERROR_CODE(int) == pss_bytecode_module_free(this->module))
		    rc = ERROR_CODE(int);

		free(this);
	}

	return rc;
}
