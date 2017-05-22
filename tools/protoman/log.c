/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <stdarg.h>

#include <log.h>

#include <proto.h>

static int _level_limit = 6;

void log_level(int value)
{
	_level_limit = value;
}

void log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
{
	if(level > _level_limit) return;
	(void)file;
	(void)function;
	(void)line;
	va_list ap;
	va_start(ap,fmt);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static inline void _print_err(const proto_err_t* err) __attribute__((used));

/**
 * @brief print the libproto error message to screen
 * @param err the error object
 * @return nothing
 **/
static inline void _print_err(const proto_err_t* err)
{
	if(NULL == err) return;

	if(NULL != err->child) _print_err(err->child);

	static char buf[1024];
	const char* msg = proto_err_str(err, buf, sizeof(buf));
	if(NULL == msg)
	    LOG_ERROR("<unknown-libproto-error>");
	else
	    LOG_ERROR("libproto: %s", msg);
}

void log_libproto_error(const char* filename, int lineno)
{
#ifdef LOG_ERROR_ENABLED
	LOG_ERROR("libproto error: at %s:%d", filename, lineno);
	_print_err(proto_err_stack());
	LOG_ERROR("==========End of Error Stack==========");
	proto_err_clear();
#else
	(void)filename;
	(void)lineno;
#endif
}
