/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/file.h>

#include <mime.h>
#include <options.h>
#include <file.h>

struct _file_ctx_t {
	pipe_t                 p_file;     /*!< The file pipe */
	pstd_type_accessor_t   a_file_tok; /*!< The accessor for the file token */
};

file_ctx_t* file_ctx_new(const options_t* options, pstd_type_model_t* type_model)
{
	if(NULL == options || NULL == type_model)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	file_ctx_t* ret = (file_ctx_t*)malloc(sizeof(file_ctx_t*));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the file reader context");

	if(ERROR_CODE(pipe_t) == (ret->p_file = pipe_define("file", PIPE_OUTPUT, "plumber/std/request_local/File")))
		ERROR_LOG_GOTO(ERR, "Cannot declare the file output pipe port");

	if(ERROR_CODE(pstd_type_accessor_t) == (ret->a_file_tok = pstd_type_model_get_accessor(type_model, ret->p_file, "token")))
		ERROR_LOG_GOTO(ERR, "Cannot get accessor of the file token");

	return ret;
ERR:
	free(ret);
	return NULL;
}

int file_ctx_free(file_ctx_t* ctx)
{
	free(ctx);
	return 0;
}

int file_ctx_exec(const file_ctx_t* file_ctx, pstd_type_instance_t* type_inst, const char* path)
{
	if(NULL == file_ctx || NULL == type_inst || NULL == path)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	/* If this path is invalid, do nothing */
	if(path[0] == 0) return 0;

	struct stat st;
	/* If we are not able to grab the file status, do nothing */
	if(ERROR_CODE(int) == pstd_fcache_stat(path, &st))
		return 0;

	/* If this is not a regular file, do nothing */
	if(!(st.st_mode & S_IFREG))
		return 0;

	pstd_file_t* rls_obj = pstd_file_new(path);
	if(NULL == rls_obj)
		ERROR_RETURN_LOG(int, "Cannot create file RLS object");

	scope_token_t token = pstd_file_commit(rls_obj);
	if(ERROR_CODE(scope_token_t) == token)
	{
		pstd_file_free(rls_obj);
		ERROR_RETURN_LOG(int, "Cannot commit the RLS object to scope");
	}

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, file_ctx->a_file_tok, token))
		ERROR_RETURN_LOG(int, "Cannot write the read result to the output pipe");

	return 0;
}
