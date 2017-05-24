/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>

typedef struct hashnode_t {
	uint64_t           hashcode;
	char*              mimetype;
	struct hashnode_t* next;
} hashnode_t;


typedef struct {
	pipe_t      extname;
	pipe_t      mimetype;

	pstd_type_model_t*   type_model;
	pstd_type_accessor_t extname_token;
	pstd_type_accessor_t mimetype_token;

	hashnode_t*          hash[997];
} context_t;

#define HASH_SIZE (sizeof(((context_t*)NULL)->hash) / sizeof(((context_t*)NULL)->hash[0]))

static inline int _ws(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static inline int _eol(char ch)
{
	return ch == '#' || ch == 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	uint32_t i;
	context_t* ctx = (context_t*)ctxbuf;
	if(argc != 2)
		ERROR_RETURN_LOG(int, "Usage: %s <path to mime.types file>", argv[0]);
	
	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot create type model");

	if(ERROR_CODE(pipe_t) == (ctx->extname = pipe_define("extname", PIPE_INPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot create pipe 'extname'");

	if(ERROR_CODE(pipe_t) == (ctx->mimetype = pipe_define("mimetype", PIPE_OUTPUT, "plumber/std/request_local/String")))
		ERROR_RETURN_LOG(int, "Cannot create pipe 'mimetype'");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->extname_token = pstd_type_model_get_accessor(ctx->type_model, ctx->extname, "token")))
		ERROR_RETURN_LOG(int, "Cannnot get the accessor for extname.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->mimetype_token = pstd_type_model_get_accessor(ctx->type_model, ctx->mimetype, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for mimetype.token");

	FILE* fp = fopen(argv[1], "r");
	if(NULL == fp) ERROR_RETURN_LOG_ERRNO(int, "Cannot open the mime type file: %s", argv[1]);

	memset(ctx->hash, 0, sizeof(ctx->hash));
	char buf[4096];
	while(NULL != fgets(buf, sizeof(buf), fp))
	{
		char* mime_begin = NULL, *mime_end = NULL;
		char* ext_begin = NULL, *ext_end = NULL;
		/* Skip the leading white space and determine the begining of the mime type section */
		for(mime_begin = buf;!_eol(*mime_begin) && _ws(*mime_begin); mime_begin ++);
		/* Search for the end of the mime type section */
		for(mime_end = mime_begin; !_eol(*mime_end) && !_ws(*mime_end); mime_end ++);

		ext_end = mime_end;

		for(;;)
		{
			/* Skip the whitespaces between the mime type and extension name */
			for(ext_begin = ext_end; !_eol(*ext_begin) && _ws(*ext_begin); ext_begin ++);
			/* The parsing ends when we get the end of the line */
			if(_eol(*ext_begin)) break;
			/* Search for the end of the extension name */
			for(ext_end = ext_begin; !_eol(*ext_end) && !_ws(*ext_end); ext_end ++);

			uint64_t hashcode = 0;
			size_t   len = (size_t)(ext_end - ext_begin);
			memcpy(&hashcode, ext_begin, (len > sizeof(hashcode)) ? sizeof(hashcode) : len);

			hashnode_t* node = (hashnode_t*)malloc(sizeof(hashnode_t));
			if(NULL == node)
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the hash table");

			node->hashcode = hashcode;
			if(NULL == (node->mimetype = (char*)malloc((size_t)(mime_end - mime_begin + 1))))
				ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the MIME type string");

			memcpy(node->mimetype, mime_begin, (size_t)(mime_end - mime_begin));

			node->mimetype[mime_end - mime_begin] = 0;

			node->next = ctx->hash[hashcode % HASH_SIZE];
			ctx->hash[hashcode % HASH_SIZE] = node;
		}
	}
	
	fclose(fp);

	return 0;
ERR:
	
	for(i = 0; i < HASH_SIZE; i ++)
	{
		hashnode_t* ptr = ctx->hash[i];
		for(;NULL != ptr;)
		{
			hashnode_t* cur = ptr;
			ptr = ptr->next;
			if(NULL != cur->mimetype) free(cur->mimetype);
			free(cur);
		}
	}

	if(NULL != fp) fclose(fp);

	if(NULL != ctx->type_model) pstd_type_model_free(ctx->type_model); 

	return ERROR_CODE(int);
}

int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	uint32_t i;
	for(i = 0; i < HASH_SIZE; i ++)
	{
		hashnode_t* ptr = ctx->hash[i];
		for(;NULL != ptr;)
		{
			hashnode_t* cur = ptr;
			ptr = ptr->next;
			if(NULL != cur->mimetype) free(cur->mimetype);
			free(cur);
		}
	}

	if(NULL != ctx->type_model) 
		return pstd_type_model_free(ctx->type_model);

	return 0;
}
static inline int _write_str(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, const char* str)
{
	pstd_string_t* pstr = pstd_string_new(32);
	if(NULL == pstr)
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the pstd_string object");
	if(ERROR_CODE(size_t) == pstd_string_write(pstr, str, strlen(str) + 1))
		ERROR_LOG_GOTO(ERR, "Cannot write the text to the pstd_string object");

	scope_token_t token = pstd_string_commit(pstr);
	if(ERROR_CODE(scope_token_t) == token)
		ERROR_LOG_GOTO(ERR, "Cannot commit the string to the RLS");
	else
		pstr = NULL;

	return PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, accessor, token);
ERR:
	if(NULL != pstr) pstd_string_free(pstr);
	return ERROR_CODE(int);
}

int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	size_t tisz = pstd_type_instance_size(ctx->type_model);
	if(ERROR_CODE(size_t) == tisz) ERROR_RETURN_LOG(int, "Cannot get the size of the type model");
	char tibuf[tisz];
	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->type_model, tibuf);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot get the type instance");

	scope_token_t token;
	if(ERROR_CODE(scope_token_t) == (token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->extname_token)))
		ERROR_LOG_GOTO(ERR, "Cannot read the scope token");

	const pstd_string_t* str = pstd_string_from_rls(token);
	if(NULL == str) 
		ERROR_LOG_GOTO(ERR, "Cannot load string from RLS");

	const char* cstr = pstd_string_value(str);
	if(NULL == cstr)
		ERROR_LOG_GOTO(ERR, "Cannot get the value of the stirng");

	size_t len = strlen(cstr);
	if(len > sizeof(uint64_t)) len = sizeof(uint64_t);

	uint64_t hashcode = 0;
	memcpy(&hashcode, cstr, len);

	const hashnode_t* node;
	const char* result = "application/octet-stream";
	for(node = ctx->hash[hashcode % HASH_SIZE]; NULL != node && node->hashcode != hashcode; node = node->next);
	if(NULL != node) result = node->mimetype;

	if(ERROR_CODE(int) == _write_str(inst, ctx->mimetype_token, result))
		ERROR_LOG_GOTO(ERR, "Cannot write the string to the output");

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance object");
	return 0;
ERR:
	if(NULL != inst) pstd_type_instance_free(inst);
	return 0;
}

SERVLET_DEF = {
	.desc = "Guess the MIME type of the given file extension name",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _unload,
	.exec = _exec
};
