/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <constants.h>
#include <utils/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <error.h>
#include <barrier.h>


__attribute__((used)) /* make sure clang won't complain about unused variables */
static char _src_root[] = __PLUMBER_SOURCE_ROOT__;
static size_t _src_root_length = sizeof(_src_root);

static char _log_path[8][PATH_MAX] = {};
static char _log_mode[8][16] = {};
static int _log_stderr[8] = {};
static FILE* _log_fp[8] = {};

static FILE _fp_off;

static pthread_mutex_t _log_mutex;

static int _log_mutex_active = 0;


int log_init()
{
	FILE* default_fp = stderr;
	const char* conf_path = getenv("LOGCONF");
	char cwd[PATH_MAX];


	if(NULL == getcwd(cwd, sizeof(cwd)))
	{
		perror("Could not get CWD");
		return ERROR_CODE(int);
	}

	if(pthread_mutex_init(&_log_mutex, NULL) < 0)
	{
		perror("mutex error");
		return ERROR_CODE(int);
	}

	if(NULL == conf_path || 0 == strlen(conf_path))
	    conf_path = CONFIG_PATH "/" LOG_DEFAULT_CONFIG_FILE;
	FILE* fp = fopen(conf_path, "r");
	strcpy(_log_path[7], "<stderr>");
	_log_fp[7] = stderr;
	if(NULL != fp)
	{
		static char buf[PATH_MAX];
		static char type[128];
		static char path[PATH_MAX];
		static char mode[32];
		int screen_print = 0;
		for(;NULL != fgets(buf, sizeof(buf), fp);)
		{
			char* begin = NULL;
			char* end = NULL;
			char* p, *q;
			for(p = buf; *p; p ++)
			{
				if(*p != '\t' &&
				   *p != ' '  &&
				   NULL == begin)
				    begin = p;
				if((*p == '#'  ||
				    *p == '\n' ||
				    *p == '\r') &&
				   NULL == end)
				    end = p;
			}
			if(NULL == begin) begin = buf;
			if(NULL == end)   end = p;
			*end = 0;
			int rc = sscanf(begin, "%s%s%s", type, path, mode);
			if(rc < 2) continue;
			if(rc == 2)
			{
				mode[0] = 'w';
				mode[1] = '0';
			}

			for(p = q = mode; *p; *(q++) = *(p++))
			    if(*p == 'e') screen_print = 1, p ++;

			*q = 0;

			int level;
#define     _STR_TO_ID(name) else if(strcmp(type, #name) == 0) level = name
			if(0);
			_STR_TO_ID(DEBUG);
			_STR_TO_ID(TRACE);
			_STR_TO_ID(INFO);
			_STR_TO_ID(NOTICE);
			_STR_TO_ID(WARNING);
			_STR_TO_ID(ERROR);
			_STR_TO_ID(FATAL);
			else if(strcmp(type, "default") == 0)
			    level = 7;
			else continue;
#undef      _STR_TO_ID

			FILE* outfile = NULL;
			if(strcmp(path, "<stdin>") == 0) outfile = stdin;
			else if(strcmp(path, "<stdout>") == 0) outfile = stdout;
			else if(strcmp(path, "<stderr>") == 0) outfile = stderr;
			else if(strcmp(path, "<disabled>") == 0) outfile = &_fp_off;
			else
			{
				int i;
				for(i = 0; i < 8 ; i ++)
				    if(_log_fp[i] != NULL && strcmp(path, _log_path[i]) == 0)
				    {
					    outfile = _log_fp[i];
					    break;
				    }
				if(NULL == outfile)
				{
					outfile = fopen(path, mode);
				}
			}
			if(_log_fp[level] != NULL)
			{
				int i;
				/* more than one log file, override */
				FILE* unused = _log_fp[level];
				for(i = 0; i < 8; i ++)
				    if(_log_fp[i] == unused && i != level)
				        break;
				if(i == 8 && unused != stdin && unused != stdout && unused != stderr && unused != &_fp_off)
				    fclose(unused);
				_log_fp[level] = NULL;
			}
			_log_fp[level] = outfile;

			snprintf(_log_path[level], sizeof(_log_path[0]), "%s%s%s", path[0] != '/' ? cwd : "", path[0] != '/' ? "/": "", path);
			snprintf(_log_mode[level], sizeof(_log_mode[0]), "%s", mode);
			_log_stderr[level] = screen_print;
		}
		if(_log_fp[7] != NULL) default_fp = _log_fp[7];
	}
	int i;
	for(i = 0; i < 7; i ++)
	{
		if(_log_fp[i] == NULL)
		{
			_log_fp[i] = default_fp;
			snprintf(_log_path[i], sizeof(_log_path[0]), "%s", _log_path[7]);
			snprintf(_log_mode[i], sizeof(_log_mode[0]), "%s", _log_mode[7]);
		}
		if(NULL == _log_fp[i])
		{
			fprintf(stderr, "fatal: can not open file for logging");
			return ERROR_CODE(int);
		}
	}
	if(NULL != fp && &_fp_off != fp) fclose(fp);

	return 0;
}
int log_finalize()
{
	int i, j;
	for(i = 0; i < 8; i ++)
	    if(_log_fp[i] != NULL &&
	       _log_fp[i] != stdin &&
	       _log_fp[i] != stdout &&
	       _log_fp[i] != stderr &&
	       _log_fp[i] != &_fp_off)
	    {
		    FILE* unused = _log_fp[i];
		    for(j = 0; j < 8; j ++)
		        if(unused == _log_fp[j])
		            _log_fp[j] = NULL;
		    fclose(unused);
	    }
	return 0;
}
void log_write(int level, const char* file, const char* function, int line, const char* fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	log_write_va(level, file, function, line, fmt, ap);
	va_end(ap);
}

int log_redirect(int level, const char* dest, const char* mode)
{
	if(level > DEBUG || level < 0)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_log_mutex_active = 1;

	BARRIER();

	if(pthread_mutex_lock(&_log_mutex) < 0)
	    return ERROR_CODE(int);

	FILE* fp = dest != NULL ? fopen(dest, mode) : &_fp_off;
	if(NULL == fp) goto ERR;

	FILE* prev = _log_fp[level];

	int i, using = 0;
	for(i = 0; i < 8 && prev != NULL; i ++)
	    if(prev == _log_fp[level])
	        using ++;
	if(using == 1 && prev != &_fp_off)
	    fclose(prev);

	_log_fp[level] = fp;

	snprintf(_log_path[level], sizeof(_log_path[level]), "%s", dest);
	snprintf(_log_mode[level], sizeof(_log_mode[level]), "%s", mode);

	if(pthread_mutex_unlock(&_log_mutex) < 0)
	    return ERROR_CODE(int);

	BARRIER();

	_log_mutex_active = 0;

	return 0;
ERR:
	pthread_mutex_unlock(&_log_mutex);

	BARRIER();

	_log_mutex_active = 0;
	return ERROR_CODE(int);
}

/**
 * @brief check if the log file has been deleted
 * @param level the level of the log message
 * @return status code
 **/
static inline int _check_log_file(int level)
{
	if(_log_fp[level] == &_fp_off || _log_fp[level] == NULL)
	    return 0;

	int fd = fileno(_log_fp[level]);

	if(-1 == fd) return ERROR_CODE(int);

	if(fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
	    return 0;

	struct stat st;
	if(fstat(fd, &st) != 0) return ERROR_CODE(int);

	int reopen = 0;
	if(st.st_nlink == 0) reopen = 1;
	else if(access(_log_path[level], F_OK) != 0) reopen = 1;

	if(reopen)
	{
		FILE* prev = _log_fp[level];
		fclose(_log_fp[level]);
		if(NULL == (_log_fp[level] = fopen(_log_path[level], _log_mode[level])))
		{
			_log_fp[level] = &_fp_off;
			return ERROR_CODE(int);
		}

		int i;
		for(i = 0; i < 8; i ++)
		    if(_log_fp[i] == prev) _log_fp[i] = _log_fp[level];
	}

	return 0;
}

/**
 * @brief check if the filename is in the plumber code base
 * @param filename the filename to check
 * @return the result
 * @note this function do not check for the null pointer
 **/
static inline int _is_framework_code(const char* filename)
{
	const char* ptr = _src_root;
	for(;*ptr && *ptr == *filename; ptr ++, filename ++);
	return !*ptr;
}

void log_write_va(int level, const char* file, const char* function, int line, const char* fmt, va_list ap)
{
	int locked = 0;
	if(_log_mutex_active && pthread_mutex_lock(&_log_mutex) < 0)
	{
		perror("mutex error");
		return;
	}
	else locked = 1;

	if(_log_fp[level] == &_fp_off) goto UNLOCK;

	if(_check_log_file(level) == ERROR_CODE(int))
	{
		perror("logging error");
		goto UNLOCK;
	}

	static const char level_char[] = "FEWNITD";
	FILE* fp = _log_fp[level];

	if(_is_framework_code(file))
	    file += _src_root_length;

	struct timespec time;

	clock_gettime(CLOCK_REALTIME, &time);

	double ts = (double)time.tv_sec + (double)time.tv_nsec / 1e+9;

	if(fp != stderr && _log_stderr[level])
	{
		va_list ap_copy;
		va_copy(ap_copy, ap);
		flockfile(stderr);
		fprintf(stderr,"%c[%16.6lf|%s@%s:%d] ", level_char[level], ts, function, file, line);
		vfprintf(stderr, fmt, ap_copy);
		fprintf(stderr, "\n");
		fflush(stderr);
		funlockfile(stderr);
	}

	flockfile(fp);
	fprintf(fp,"%c[%16.6lf|%s@%s:%d] ", level_char[level], ts, function, file, line);
	vfprintf(fp, fmt, ap);
	fprintf(fp, "\n");
	fflush(fp);
	funlockfile(fp);

UNLOCK:
	if(locked && pthread_mutex_unlock(&_log_mutex) < 0)
	    perror("mutex error");
}
