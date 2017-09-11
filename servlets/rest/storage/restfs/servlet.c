/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <json.h>

#include <pservlet.h>
#include <pstd.h>

typedef struct {
	uint32_t json_mode:1;        /*!< The JSON mode */
	uint32_t modify_time:1;      /*!< If we need change time */
	uint32_t create_time:1;      /*!< If we need the creating time */
	pipe_t  command;             /*!< The storage command input pipe */
	pipe_t  parent_not_exist;    /*!< The input side of the signal for the parent resource is not exist */
	pipe_t  not_exist;           /*!< The signal pipe which will trigger when the exist command is required and the resource is not avaliable */
	pipe_t  data;                /*!< The actual resoure data or the list of resource id in JSON / the raw file RLS token  */
} context_t;

static int _ts_option(pstd_option_data_t data)
{
	context_t* ctx = (context_t*)data.cb_data;
	switch(data.current_option->short_opt)
	{
		case 'c':
			ctx->create_time = 1;
			break;
		case 'm':
			ctx->modify_time =1;
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid command line parameter");
	}
	return 0;
}

static int _process_json_schema(pstd_option_data_t data)
{
	context_t* ctx = (context_t*)data.cb_data;
	ctx->json_mode = 1;
	if(data.param_array_size == 0)
	{
		/* The schemaless mode */
		LOG_DEBUG("The servlet is configured to a schemaless mode");
	}
	else if(data.param_array_size != 1 || data.param_array == NULL || data.param_array[0].type != PSTD_OPTION_STRING)
		ERROR_RETURN_LOG(int, "Invalid arguments use --help to see the usage");
	else
	{
		struct stat st;
		if(stat(data.param_array[0].strval, &st) < 0)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot stat the schema file %s", data.param_array[0].strval);
		size_t size = (size_t)st.st_size;
		if(size == 0) ERROR_RETURN_LOG(int, "Invalid schema file %s: schema is empty", data.param_array[0].strval);
		char* buf = (char*)malloc(size + 1);
		FILE* fp = NULL;
		json_object* schema_obj = NULL;
		if(NULL == buf)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the schema text");
		if(NULL == (fp = fopen(data.param_array[0].strval, "r")))
			ERROR_LOG_ERRNO_GOTO(SCHEMA_ERR, "Cannot open the schema file %s", data.param_array[0].strval);
		
		if(fread(buf, size, 1, fp) != 1) ERROR_LOG_ERRNO_GOTO(SCHEMA_ERR, "Cannot read the schema file %s", data.param_array[0].strval);
		fclose(fp);
		fp = NULL;
		buf[size] = 0;

		if(NULL == (schema_obj = json_tokener_parse(buf)))
			ERROR_LOG_GOTO(SCHEMA_ERR, "Invalid schema file %s: Syntax error", data.param_array[0].strval);
		free(buf);
		buf = NULL;

		/* TODO: do something with the schema_obj */
		json_object_iter iter;

		json_object_object_foreachC(schema_obj, iter)
		{
			LOG_DEBUG("%s", iter.key);
			LOG_DEBUG("%s", json_object_get_string(iter.val));
		}
		
		json_object_put(schema_obj);
		return 0;

SCHEMA_ERR:
		if(NULL != fp) fclose(fp);
		if(NULL != buf) free(buf);
		if(NULL != schema_obj) json_object_put(schema_obj);
		return ERROR_CODE(int);
	}
	
	return 0;
}

static pstd_option_t _options[] = {
	{
		.long_opt  = "help",
		.short_opt = 'h',
		.pattern   = "",
		.description = "Print this help message",
		.handler = pstd_option_handler_print_help,
		.args = NULL
	},
	{
		.long_opt  = "json",
		.short_opt = 'j',
		.pattern   = "?S",
		.description = "The servlet process JSON data with the given data schema",
		.handler     = _process_json_schema,
		.args        = NULL
	},
	{
		.long_opt  = "create-timestamp",
		.short_opt = 'c',
		.pattern   = "",
		.description = "If we need to add the creation timestamp autoamtically to the data",
		.handler     = _ts_option,
		.args        = NULL
	},
	{
		.long_opt  = "modify-timestamp",
		.short_opt = 'm',
		.pattern   = "",
		.description = "If we need to add modification timestamp automatically to the data",
		.handler     = _ts_option,
		.args        = NULL
	}
};

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	(void)argc;
	(void)argv;
	context_t* ctx = (context_t*)ctxbuf;

	if(ERROR_CODE(int) == pstd_option_sort(_options, sizeof(_options) / sizeof(_options[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the options");

	uint32_t rc;
	if(ERROR_CODE(uint32_t) == (rc = pstd_option_parse(_options, sizeof(_options) / sizeof(_options[0]), argc, argv, ctx)))
		ERROR_RETURN_LOG(int, "Cannot parse the command line param");

	if(rc != argc) ERROR_RETURN_LOG(int, "Invalid command arguments");

	if(ERROR_CODE(pipe_t) == (ctx->command = pipe_define("command", PIPE_INPUT, "plumber/std_servlet/rest/restcon/v1/Command")))
		ERROR_RETURN_LOG(int, "Cannot define the command input pipe");
	if(ERROR_CODE(pipe_t) == (ctx->parent_not_exist = pipe_define("parent_not_exist", PIPE_INPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the signal pipe for the parent not exists");
	if(ERROR_CODE(pipe_t) == (ctx->not_exist = pipe_define("not_exist", PIPE_OUTPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the signal pipe for the resource not exists event");
	if(ERROR_CODE(pipe_t) == (ctx->data = pipe_define("data", PIPE_OUTPUT, "plumber/std/request_local/MemoryObject")))
		ERROR_RETURN_LOG(int, "Cannot define the data output");

	/* TODO: it seems we could have two mode, JSON mode and raw data mode,
	 *       For the json mode, we need to provide a schema file so that the servlet
	 *       can validate the input is valid. also we can make it automatically add creation time
	 *       and/or modification time 
	 *       For the raw data we don't need these options 
	 **/

	return 0;
}
SERVLET_DEF = {
	.desc    = "The filesystem based restful storage controller",
	.version = 0,
	.size    = sizeof(context_t),
	.init    = _init
};
