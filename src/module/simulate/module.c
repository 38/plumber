/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <error.h>
#include <utils/log.h>
#include <itc/module_types.h>
#include <module/simulate/module.h>

/**
 * @brief represent a simulated event, which is defined by the event file
 **/
typedef struct _event_t {
	char*                 label;  /*!< The label for this event t */
	size_t                size;   /*!< The size of this event */
	char*                 data;   /*!< The data for this event */
	struct _input_data_t* next;   /*!< The next event in the event list */
} _event_t;

/**
 * @brief The module context 
 **/
typedef struct {
	_event_t*      event_list;  /*!< The event we want to simulate */
	FILE*          outfile;     /*!< The file we want to dump the output to */
} _module_context_t;

/**
 * @brief The pipe handle
 **/
typedef struct {
	const _event_t*      event;   /*!< The event we are handling */
	size_t               offset;  /*!< The offset for where we are */
} _handle_t;

static inline int _start_with(const char* str, const char* pref)
{
	for(;*str != 0 && *pref != 0 && *str == *pref;str ++, pref++);
	return *pref == 0;
}

static inline int _parse_comment(FILE* fp)
{
	for(;;)
	{
		int ch = fgetc(fp);
		if(EOF == ch && ferror(fp)) ERROR_RETURN_LOG_ERRNO(int, "Cannot read file");
		if(EOF == ch || ch == '\n') break;
	}

	return 0;
}

#if 0
static inline int _parse_command(_module_context_t* ctx, FILE* fp)
{
	char buf[1024];
	fgets(buf, sizeof(buf), fp)
}
#endif

static inline int _parse_input(_module_context_t* ctx, FILE* fp)
{
	(void)ctx;
	for(;;)
	{
		int ch = fgetc(fp);
		if(EOF == ch)
		{
			if(ferror(fp)) ERROR_RETURN_LOG_ERRNO(int, "Cannot read file");
			else break;
		}

		switch(ch) 
		{
			case '#':
				if(ERROR_CODE(int) == _parse_comment(fp))
					ERROR_RETURN_LOG(int, "Cannot parse the comment");
				break;
			case '.':  /* TODO */
#if 0
				if(ERROR_CODE(int) == _parse_command(ctx, fp))
					ERROR_RETURN_LOG(int, "Cannot parse the command");
#endif
				break;
			default:
				ERROR_RETURN_LOG(int, "Invalid sytax");
		}
	}

	return 0;
}

static int _init(void* __restrict ctxbuf, uint32_t argc, char const* __restrict const* __restrict argv)
{
	_module_context_t* ctx = (_module_context_t*)ctxbuf;
	if(NULL == ctx) ERROR_RETURN_LOG(int, "Invalid arguments");

	const char input_prefix[] = "input=";
	const char output_prefix[] = "output=";

	const char* input_name = NULL;
	const char* output_name = NULL;

	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		if(_start_with(argv[i], input_prefix)) input_name = argv[i] + sizeof(input_prefix) - 1;
		else if(_start_with(argv[i], output_prefix)) output_name = argv[i] + sizeof(output_name) - 1;
		else ERROR_RETURN_LOG(int, "Invalid module initialization arguments, expected: simulate input=inputfile output=outputfile");
	}

	if(input_name == NULL || input_name[0] == 0)
		ERROR_RETURN_LOG(int, "Missing input file name");
	if(output_name == NULL || output_name[0] == 0)
		ERROR_RETURN_LOG(int, "Missing output file name");

	/* Then let's parse the input file */
	FILE* fp = fopen(input_name, "r");
	if(NULL == fp) ERROR_RETURN_LOG_ERRNO(int, "Cannot open the input file %s", input_name);

	if(ERROR_CODE(int) == _parse_input(ctx, fp))
	{
		fclose(fp);
		ERROR_RETURN_LOG(int, "Cannot parse the input file %s", input_name);
	}

	fclose(fp);
	/* TODO */
	return 0;
}


itc_module_t module_simulate_module_def = {
	.mod_prefix   = "pipe.simulate",
	.context_size = sizeof(_module_context_t),
	.handle_size  = sizeof(_handle_t),
	.module_init  = _init
};
