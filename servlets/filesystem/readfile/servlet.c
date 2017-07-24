/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#include <constants.h>

#include <pservlet.h>
#include <pstd.h>
#include <proto.h>

#include <pstd/types/string.h>
#include <pstd/types/file.h>

typedef struct {
	uint32_t token_ofs; /*!< The offset of the token */
	char*    root;      /*!< The root directory */
	size_t   root_len;  /*!< The length of the root */
	pipe_t   path;      /*!< The path to the file to read */
	pipe_t   result;    /*!< The read result of the file */
	pstd_type_model_t*   type_model;    /*!< The servlet type model */
	pstd_type_accessor_t path_token;    /*!< The RLS token to the path */
	pstd_type_accessor_t file_token;    /*!< The RLS token to the file */
	pstd_type_accessor_t redirect_token;/*!< The RLS token to the redirect path */
	pstd_type_accessor_t status;        /*!< The status code */

	uint32_t             STATUS_OK;   /*!< The ok status */
	uint32_t             STATUS_MOVED;
	uint32_t             STATUS_NOT_FOUND;
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{

#ifndef LOG_ERROR_ENABLED
	(void)argv;
#endif /* LOG_ERROR_ENABLED */

	int ret = ERROR_CODE(int);

	if(argc != 2)
	    ERROR_RETURN_LOG(int, "Usage: %s <root-directory>", argv[1]);

	context_t* ctx = (context_t*)ctxbuf;



	if(ERROR_CODE(pipe_t) == (ctx->path = pipe_define("path", PIPE_INPUT, "plumber/std/request_local/String")))
	    ERROR_RETURN_LOG(int, "Cannot create input pipe");

	if(ERROR_CODE(pipe_t) == (ctx->result = pipe_define("result", PIPE_OUTPUT, "plumber/std_servlet/filesystem/readfile/Result")))
	    ERROR_RETURN_LOG(int, "Cannot create the output pipe");

	ctx->type_model = NULL;

	size_t rlen = ctx->root_len = strlen(argv[1]);
	if(rlen >= PATH_MAX - 1)
	    ERROR_RETURN_LOG(int, "The root path is too long");
	if(NULL == (ctx->root = (char*)malloc(rlen + 1)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the readfile root");
	memcpy(ctx->root, argv[1], rlen + 1);

	if(NULL == (ctx->type_model = pstd_type_model_new()))
	    ERROR_RETURN_LOG(int, "Cannot create type model for servlet");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->path_token = pstd_type_model_get_accessor(ctx->type_model, ctx->path, "token")))
	    ERROR_LOG_GOTO(ERR, "Cannot get the accessor for path.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->file_token = pstd_type_model_get_accessor(ctx->type_model, ctx->result, "file.token")))
	    ERROR_LOG_GOTO(ERR, "Cannot get the accessor for result.file.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->redirect_token = pstd_type_model_get_accessor(ctx->type_model, ctx->result, "redirect.token")))
	    ERROR_LOG_GOTO(ERR, "Cannot get the accessor for result.redirect.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->status = pstd_type_model_get_accessor(ctx->type_model, ctx->result, "status")))
	    ERROR_LOG_GOTO(ERR, "Cannot get the accessor for result.status");

	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->result, "STATUS_OK", &ctx->STATUS_OK))
	    ERROR_LOG_GOTO(ERR, "Cannot get the constant STATUS_OK");
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->result, "STATUS_MOVED", &ctx->STATUS_MOVED))
	    ERROR_LOG_GOTO(ERR, "Cannot get the constant STATUS_OK");
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(ctx->type_model, ctx->result, "STATUS_NOT_FOUND", &ctx->STATUS_NOT_FOUND))
	    ERROR_LOG_GOTO(ERR, "Cannot get the constant STATUS_OK");

	ret = 0;
ERR:

	if(ERROR_CODE(int) == ret && NULL != ctx->type_model)
	    pstd_type_model_free(ctx->type_model);

	if(ERROR_CODE(int) == ret && NULL != ctx->root)
	    free(ctx->root);

	return ret;
}
static int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	int rc = 0;
	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
	    rc = ERROR_CODE(int);

	free(ctx->root);
	return  rc;
}

static inline const char* _read_string(pstd_type_instance_t* inst, pstd_type_accessor_t accessor)
{
	scope_token_t token;
	if(ERROR_CODE(scope_token_t) == (token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, accessor)))
	    ERROR_PTR_RETURN_LOG("Cannot access path.token");

	const pstd_string_t* pstr = pstd_string_from_rls(token);
	if(NULL == pstr) ERROR_PTR_RETURN_LOG("Cannot retrive string object from the RLS");

	return pstd_string_value(pstr);
}

static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	size_t tisz = pstd_type_instance_size(ctx->type_model);
	uint32_t status;
	if(ERROR_CODE(size_t) == tisz) ERROR_RETURN_LOG(int, "Cannot get the size of the type instance");
	char tibuf[tisz];
	pstd_file_t* file = NULL;
	pstd_string_t* redir = NULL;
	scope_token_t file_token, redir_token;
	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->type_model, tibuf);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create type instance");

	const char* path = _read_string(inst, ctx->path_token);
	if(NULL == path) ERROR_LOG_GOTO(ERR, "Cannot get the path");

	char pathbuf[PATH_MAX];
	size_t len = ctx->root_len;
	if(len > 0) memcpy(pathbuf, ctx->root, len);

	size_t path_len = strlen(path);
	if(path_len + len + 1 >= sizeof(pathbuf)) goto RET_404;
	memcpy(pathbuf + len, path, path_len + 1);
	len += path_len;

	struct stat st;
	if(pstd_fcache_stat(pathbuf, &st) == ERROR_CODE(int)) goto RET_404;

	if(!(st.st_mode & S_IFREG))
	{
		if(len > 0 && pathbuf[len - 1] != '/') pathbuf[len ++] = '/';

		static const char index_file[] = "index.html";
		if(sizeof(pathbuf) < sizeof(index_file) + len) goto RET_404;
		memcpy(pathbuf + len, index_file, sizeof(index_file));

		if(pstd_fcache_stat(pathbuf, &st) == ERROR_CODE(int)) goto RET_404;
		else
		{
			if(NULL == (redir = pstd_string_new(sizeof(pathbuf))))
			    ERROR_LOG_GOTO(ERR, "Cannot create redirect path");
			if(ERROR_CODE(size_t) == pstd_string_printf(redir, "%s", pathbuf + ctx->root_len))
			    ERROR_LOG_GOTO(ERR, "Cannot write path to string");

			if(ERROR_CODE(scope_token_t) == (redir_token = pstd_string_commit(redir)))
			    ERROR_LOG_GOTO(ERR, "Cannot commit the redirect string to the token");
			else redir = NULL;
			goto RET_302;
		}
	}

	if(NULL == (file = pstd_file_new(pathbuf)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the file object");

	if(ERROR_CODE(scope_token_t) == (file_token = pstd_file_commit(file)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the file RLS token");
	else
	    file = NULL;

	goto RET_200;
RET_404:
	status = ctx->STATUS_NOT_FOUND;
	goto RET;
RET_302:
	status = ctx->STATUS_MOVED;

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->redirect_token, redir_token))
	    ERROR_LOG_GOTO(ERR, "Cannot write redirect.token");

	goto RET;
RET_200:
	status = ctx->STATUS_OK;

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->file_token, file_token))
	    ERROR_LOG_GOTO(ERR, "Cannot write file.token");

	goto RET;
RET:
	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->status, status))
	    ERROR_LOG_GOTO(ERR, "Cannot write status");
	return pstd_type_instance_free(inst);
ERR:
	if(NULL != file) pstd_file_free(file);
	if(NULL != redir) pstd_string_free(redir);
	pstd_type_instance_free(inst);
	return ERROR_CODE(int);
}


SERVLET_DEF = {
	.desc = "Read the file sepecified in the input pipe under given directory",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
