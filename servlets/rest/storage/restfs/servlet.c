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
#include <jsonschema.h>
#include <jsonschema/log.h>

typedef struct {
	uint32_t json_mode:1;        /*!< The JSON mode */
	uint32_t modify_time:1;      /*!< If we need change time */
	uint32_t create_time:1;      /*!< If we need the creating time */
	jsonschema_t* schema;        /*!< The schema of this resource, if this field is NULL, we are in schema-less mode */
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
		ctx->schema = NULL;
		LOG_DEBUG("The servlet is configured to a schemaless mode");
	}
	else if(data.param_array_size != 1 || data.param_array == NULL || data.param_array[0].type != PSTD_OPTION_STRING)
		ERROR_RETURN_LOG(int, "Invalid arguments use --help to see the usage");
	else
	{
		const char* schema_file = data.param_array[0].strval;

		if(NULL == (ctx->schema = jsonschema_from_file(schema_file)))
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
	context_t* ctx = (context_t*)ctxbuf;

	ctx->schema = NULL;

	jsonschema_log_set_write_callback(RUNTIME_ADDRESS_TABLE_SYM->log_write);

	if(ERROR_CODE(int) == pstd_option_sort(_options, sizeof(_options) / sizeof(_options[0])))
		ERROR_RETURN_LOG(int, "Cannot sort the options");

	uint32_t rc;
	if(ERROR_CODE(uint32_t) == (rc = pstd_option_parse(_options, sizeof(_options) / sizeof(_options[0]), argc, argv, ctx)))
		ERROR_RETURN_LOG(int, "Cannot parse the command line param");

	if(rc != argc) ERROR_RETURN_LOG(int, "Invalid command arguments");

	if(ERROR_CODE(pipe_t) == (ctx->command = pipe_define("command", PIPE_INPUT, "plumber/std_servlet/rest/restcon/v0/Command")))
		ERROR_RETURN_LOG(int, "Cannot define the command input pipe");
	if(ERROR_CODE(pipe_t) == (ctx->parent_not_exist = pipe_define("parent_not_exist", PIPE_INPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the signal pipe for the parent not exists");
	if(ERROR_CODE(pipe_t) == (ctx->not_exist = pipe_define("not_exist", PIPE_OUTPUT, NULL)))
		ERROR_RETURN_LOG(int, "Cannot define the signal pipe for the resource not exists event");
	if(ERROR_CODE(pipe_t) == (ctx->data = pipe_define("data", PIPE_OUTPUT, "plumber/std/request_local/MemoryObject")))
		ERROR_RETURN_LOG(int, "Cannot define the data output");

	return 0;
}

static int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	if(NULL != ctx->schema && ERROR_CODE(int) == jsonschema_free(ctx->schema))
		ERROR_RETURN_LOG(int, "Cannot dispose the JSON schema");

	return 0;
}
SERVLET_DEF = {
	.desc    = "The filesystem based restful storage controller",
	.version = 0,
	.size    = sizeof(context_t),
	.init    = _init,
	.unload  = _unload
};
