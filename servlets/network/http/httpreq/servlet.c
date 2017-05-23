/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <httpreq.h>
#include <options.h>

#if 0
#include <proto.h>
#include <protoapi.h>
#endif

/**
 * @brief the enum type used for the parser state
 **/
typedef enum {
	_ERROR = -1,           /*!< the error state */
	_EXPECT_VERB,          /*!< the parser is expecting a verb */
	_EXPECT_PATH,          /*!< the parser is excepting a path */
	_EXPECT_VERSION,       /*!< the parser is expecting a version number */
	_EXPECT_FIRST_CRLF,    /*!< the parser is expecting the first CRLF */
	_DONE                  /*!< the parsing is done */
} _state_t;

/**
 * @brief the servlet context
 **/
typedef struct {
	httpreq_options_t* options;   /*!< the servlet initialization param */
	pipe_t request;               /*!< the input pipe */
	pipe_t method;                /*!< the method contains the method (either binary or plain text) */
	pipe_t host;                  /*!< the host name pipe*/
	pipe_t path;                  /*!< the path name pipe*/
	pipe_t cookie;                /*!< the cookie pipe */
	pipe_t error;                 /*!< the error pipe */
} _servlet_conf_t;

/**
 * @brief the HTTP parser state
 **/
typedef struct {
	_state_t       code;        /*!< the high level state of the parser */
	httpreq_verb_t method;      /*!< the method verb in the request */
	uint32_t       keepalive:1; /*!< if this request is a persist connection request */
	uint32_t       empty:1;     /*!< if this is a request contains nothing */
} _parser_state_t;

static inline _parser_state_t* _parser_state_new()
{
	/* TODO: use the memory pool */
	_parser_state_t* ret = (_parser_state_t*)malloc(sizeof(_parser_state_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new state");
	ret->code  = _EXPECT_VERB;
	ret->empty = 1;
	return ret;
}

static inline int _parser_state_free(_parser_state_t* mem)
{
	if(NULL == mem) ERROR_RETURN_LOG(int, "Invalid arguments");

	/* TODO: use the memory pool */
	free(mem);

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* context)
{
	_servlet_conf_t* conf = (_servlet_conf_t*)context;
	if(NULL == (conf->options = httpreq_options_parse(argc, argv)))
	    ERROR_RETURN_LOG(int, "Cannot parse the servlet arguments");

	/* Define the servlet pipes */
	conf->request = pipe_define("request", PIPE_INPUT, NULL);
	conf->method = conf->options->produce_method ? pipe_define("method", PIPE_OUTPUT, NULL) : ERROR_CODE(pipe_t);
	conf->host = conf->options->produce_host ? pipe_define("host", PIPE_OUTPUT, NULL) : ERROR_CODE(pipe_t);
	conf->path = conf->options->produce_path ? pipe_define("path", PIPE_OUTPUT, NULL) : ERROR_CODE(pipe_t);
	conf->cookie = conf->options->produce_cookie ? pipe_define("cookie", PIPE_OUTPUT, NULL) : ERROR_CODE(pipe_t);
	conf->error = pipe_define("error", PIPE_OUTPUT, NULL);

#if 0
	proto_init();
	uint32_t test;
	PROTOAPI_OFFSET_OF_SCALAR(float, test, "graphics/FlattenColoredTriangle3D", "y1");
	LOG_FATAL("test = %u", test);
	proto_finalize();
#endif
	return 0;
}

static int _exec(void* context)
{
	int new_state = 0;
	_servlet_conf_t* conf = (_servlet_conf_t*)context;

	char buffer[4096];   /* TODO: make this configurable */
	_parser_state_t* state;

	if(ERROR_CODE(int) == pipe_cntl(conf->request, PIPE_CNTL_PUSH_STATE, &state))
	    ERROR_RETURN_LOG(int, "pipe_cntl call returns an error status code");

	if(NULL == state)
	{
		if(NULL == (state = _parser_state_new()))
		    ERROR_RETURN_LOG(int, "Cannot allocate new parser state for a new incoming connection");
		new_state = 1;
	}

	while(state->code != _DONE)
	{
		size_t sz = pipe_read(conf->request, buffer, sizeof(buffer));

		if(sz == ERROR_CODE(size_t))
		{
			if(new_state) _parser_state_free(state);
			ERROR_RETURN_LOG(int, "Cannot read from the input pipe");
		}
		else if(sz == 0)
		{
			int rc = pipe_eof(conf->request);
			if(ERROR_CODE(int) == rc)
			{
				if(new_state) _parser_state_free(state);
				ERROR_RETURN_LOG(int, "Cannot check if the pipe reached the end of stream");
			}
			else if(rc > 0)
			{
				/* If this is the end of the stream, don't keep the connection and if it's empty return directly */
				state->keepalive = 0;
				if(state->empty)
				{
					if(new_state) _parser_state_free(state);
					return 0;
				}
				break;
			}
			else
			{
				/* we need to wait for the incoming data */
				if(ERROR_CODE(int) == pipe_cntl(conf->request, PIPE_CNTL_SET_FLAG, PIPE_PERSIST))
				    goto WAIT_ERR;
				if(ERROR_CODE(int) == pipe_cntl(conf->request, PIPE_CNTL_PUSH_STATE, state))
				    goto WAIT_ERR;
				return 0;
WAIT_ERR:
				ERROR_RETURN_LOG(int, "Cannot set the task into wait state");
			}
		}
		else
		{
			state->empty = 0;
			/* TODO: scan the request */

			(void)buffer;

			if(state->code == _DONE /* && has data not processed */)
			    LOG_DEBUG("EOM!")/* pipe_cntl(conf->request, PIPE_CNTL_EOM, <the offset of unprocessed data>) */;

			if(state->code == _ERROR)
			    state->keepalive = 0;
		}
	}

	if(new_state == 1)
	    _parser_state_free(state);

	return 0;
}

static int _unload(void* context)
{
	int rc = 0;
	_servlet_conf_t* conf = (_servlet_conf_t*)context;
	if(NULL != conf->options && httpreq_options_free(conf->options) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot dispose the options object");
		rc = ERROR_CODE(int);
	}

	return rc;
}

SERVLET_DEF = {
	.desc = "HTTP Rquest Parser",
	.version = 0x0,
	.size = sizeof(_servlet_conf_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
