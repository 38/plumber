/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <proto/err.h>

/**
 * @brief error description of error code
 **/
static char* _err_desc[] = {
	[PROTO_ERR_CODE_ALLOC]        = "Allocate memory error",
	[PROTO_ERR_CODE_OPEN]         = "Open file error",
	[PROTO_ERR_CODE_READ]         = "Read file error",
	[PROTO_ERR_CODE_WRITE]        = "Write file error",
	[PROTO_ERR_CODE_FORMAT]       = "File format error",
	[PROTO_ERR_CODE_FILEOP]       = "File operation error",
	[PROTO_ERR_CODE_ARGUMENT]     = "Invalid arguemnts",
	[PROTO_ERR_CODE_FAIL]         = "Secondary error",
	[PROTO_ERR_CODE_DISALLOWED]   = "Not allowed",
	[PROTO_ERR_CODE_NOT_FOUND]    = "Type not found",
	[PROTO_ERR_CODE_CIRULAR_DEP]  = "Cirular dependency in protodb",
	[PROTO_ERR_CODE_NAME_EXPR]    = "Invalid name expression",
	[PROTO_ERR_CODE_BUG]          = "Code bug!",
	[PROTO_ERR_CODE_UNDEFINED]    = "Undefined symbol",
	[PROTO_ERR_CODE_DIM]          = "Dimensional error",
	[PROTO_ERR_CODE_OUT_OF_BOUND] = "Index out of boundary",
	[PROTO_ERR_CODE_VERSION]      = "Unsupported version"
};

/**
 * @brief the thread local state that is used to tracking the error code
 **/
static __thread proto_err_t* _stack = NULL;

const proto_err_t* proto_err_stack()
{
	return _stack;
}

void proto_err_clear()
{
	for(;NULL != _stack;)
	{
		proto_err_t* next = (proto_err_t*)_stack->child;
		free(_stack);
		_stack = next;
	}
}

void proto_err_raise(proto_err_code_t code, uint32_t line, const char* file)
{
	proto_err_t* err = (proto_err_t*)malloc(sizeof(proto_err_t));
	if(NULL == err) fprintf(stderr, "Cannot allocate memory for error info: %s", strerror(errno));

	err->code = code;
	err->line = line;
	err->file = file;
	err->errnum = _stack == NULL ? errno : 0;

	err->child = _stack;

	_stack = err;
}

const char* proto_err_str(const proto_err_t* error, char* buffer, size_t bufsize)
{
	if(NULL == error || NULL == buffer || error->code >= PROTO_ERR_CODE_COUNT)
	    return NULL;

	if(error->errnum != 0)
	    snprintf(buffer, bufsize, "%s: %s (line: %d, file: %s)",
	             _err_desc[error->code], strerror(error->errnum),
	             error->line, error->file == NULL ? "unknown" : error->file);
	else
	    snprintf(buffer, bufsize, "%s (line: %d, file: %s)",
	             _err_desc[error->code],
	             error->line, error->file == NULL ? "unknown" : error->file);

	return buffer;
}
