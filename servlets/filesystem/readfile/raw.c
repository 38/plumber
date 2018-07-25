/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include  <mime.h>
#include  <options.h>
#include  <raw.h>

struct _raw_ctx_t {
	pipe_t                 p_file;     /*!< The file pipe */
};

raw_ctx_t* raw_ctx_new(const options_t* options, pstd_type_model_t* type_model)
{
	if(NULL == options || NULL == type_model)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	raw_ctx_t* ret = (raw_ctx_t*)malloc(sizeof(raw_ctx_t*));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the raw reader context");

	if(ERROR_CODE(pipe_t) == (ret->p_file = pipe_define("file", PIPE_OUTPUT, NULL)))
		ERROR_LOG_GOTO(ERR, "Cannot declare the raw output pipe port");

	return ret;
ERR:
	free(ret);
	return NULL;
}

int raw_ctx_free(raw_ctx_t* ctx)
{
	free(ctx);
	return 0;
}

int raw_ctx_exec(const raw_ctx_t* raw_ctx, pstd_type_instance_t* type_inst, const char* path)
{
	if(NULL == raw_ctx || NULL == type_inst || NULL == path)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	/* If this path is invalid, do nothing */
	if(path[0] == 0) return 0;

	struct stat st;
	/* If we are not able to grab the raw status, do nothing */
	if(ERROR_CODE(int) == pstd_fcache_stat(path, &st))
		return 0;

	/* If this is not a regular raw, do nothing */
	if(!(st.st_mode & S_IFREG))
		return 0;

	char buf[4096];

	pstd_fcache_file_t* fp = pstd_fcache_open(path);
	if(NULL == fp) ERROR_RETURN_LOG(int, "Cannot open file %s", path);

	for(;;)
	{
		int eof_rc = pstd_fcache_eof(fp);
		if(ERROR_CODE(int) == eof_rc)
			ERROR_LOG_GOTO(ERR, "Cannot check if the file has been read completely");

		if(eof_rc) break;

		size_t bytes_read = pstd_fcache_read(fp, buf, sizeof(buf));
		if(ERROR_CODE(size_t) == bytes_read)
			ERROR_LOG_GOTO(ERR, "Cannot read the file");

		size_t bytes_written = 0;
		while(bytes_written < bytes_read)
		{
			size_t write_rc = pipe_write(raw_ctx->p_file, buf + bytes_written, bytes_read - bytes_written);
			if(ERROR_CODE(size_t) == write_rc)
				ERROR_LOG_GOTO(ERR, "Cannot write the file content to pipe");

			bytes_written += write_rc;
		}
	}

	if(ERROR_CODE(int) == pstd_fcache_close(fp))
		ERROR_RETURN_LOG(int, "Cannot close the cached file");

	return 0;
ERR:
	pstd_fcache_close(fp);
	return ERROR_CODE(int);
}
