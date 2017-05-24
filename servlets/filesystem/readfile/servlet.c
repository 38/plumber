/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <string.h>

#include <pservlet.h>
#include <pstd.h>
#include <proto.h>

typedef struct {
	uint32_t token_ofs; /*!< The offset of the token */
	pipe_t   path;      /*!< The path to the file to read */
	pipe_t   result;    /*!< The read result of the file */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	int ret = ERROR_CODE(int);

	if(argc != 2) ERROR_RETURN_LOG(int, "Usage: %s <root-directory>", argv[1]);

	context_t* ctx = (context_t*)ctxbuf;

	static const char* strtype = "plumber/std/request_local/String";

	if(ERROR_CODE(pipe_t) == (ctx->path = pipe_define("path", PIPE_INPUT, strtype)))
	    ERROR_RETURN_LOG(int, "Cannot create input pipe");

	if(ERROR_CODE(pipe_t) == (ctx->result = pipe_define("result", PIPE_OUTPUT, strtype)))
	    ERROR_RETURN_LOG(int, "Cannot create the output pipe");

	if(ERROR_CODE(int) == proto_init())
	    ERROR_RETURN_LOG(int, "Cannot initialize the libproto");

	if(ERROR_CODE(uint32_t) == (ctx->token_ofs = proto_db_type_offset(strtype, "token", NULL)))
	    ERROR_LOG_GOTO(RET, "Cannot get the offset of token in the string type");

	ret = 0;
RET:
	if(ERROR_CODE(int) == proto_finalize())
	    ERROR_RETURN_LOG(int, "Cannot finalize the libproto");

	return ret;
}


SERVLET_DEF = {
	.desc = "Read the file sepecified in the input pipe under given directory",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = NULL,
	.unload = NULL
};
