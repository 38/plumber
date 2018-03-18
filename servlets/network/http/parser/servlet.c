/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <proto.h>
#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>

#include <trie.h>
#include <routing.h>
#include <options.h>
#include <parser.h>

#define _TYPE_ROOT "plumber/std_servlet/network/http/parser/v0/"

typedef struct {
	pipe_t             p_input;          /*!< The input pipe for raw request */
	pipe_t             p_protocol_data;  /*!< The protocol related data */

	options_t          options;     /*!< The options */
	pstd_type_model_t* type_model;  /*!< The type model */

	pstd_type_accessor_t a_accept_encoding;      /*!< The accessor for the accept encoding */
	pstd_type_accessor_t a_upgrade_target;       /*!< The accessor for the HTTPS upgrade target */
	pstd_type_accessor_t a_error;                /*!< THe protocol error bits */

	uint32_t           METHOD_GET;      /*!< The method code for GET */
	uint32_t           METHOD_POST;     /*!< The method code for GET */
	uint32_t           METHOD_HEAD;     /*!< The method code for GET */

	uint64_t           RANGE_SEEK_SET;  /*!< The constant used to represent the head of the file */
	uint64_t           RANGE_SEEK_END;  /*!< THe constant used to represent the tail of the file */

	uint32_t           ERROR_NONE;      /*!< The constant indeicates that we have no error */
	uint32_t           ERROR_BAD_REQ;   /*!< THe constant indicates that we have an bad error */
} ctx_t;

static inline int _read_const_unsigned(const char* field, void* result, size_t sz)
{
	proto_db_field_prop_t prop = proto_db_field_type_info(_TYPE_ROOT"RequestData", field);

	if(ERROR_CODE(proto_db_field_prop_t) == prop)
	    ERROR_RETURN_LOG(int, "Cannot read the constant field");

	if((prop & PROTO_DB_FIELD_PROP_NUMERIC) && !(prop & PROTO_DB_FIELD_PROP_REAL) && !(prop & PROTO_DB_FIELD_PROP_REAL))
	{
		const void* default_data;
		size_t default_data_size;

		if(proto_db_field_get_default(_TYPE_ROOT"RequestData",
		                              field,
		                              &default_data,
		                              &default_data_size) == ERROR_CODE(int))
		    ERROR_RETURN_LOG(int, "Cannot read the value from the constant field");

		if(default_data_size != sz)
		    ERROR_RETURN_LOG(int, "Invalid constant size");

		memcpy(result, default_data, sz);
	}

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	uint32_t i;
	static char const * const const_names[] = {"METHOD_GET", "METHOD_POST", "METHOD_HEAD", "SEEK_SET", "SEEK_END"};
	static size_t const const_sizes[] = {sizeof(uint32_t), sizeof(uint32_t), sizeof(uint32_t), sizeof(uint64_t), sizeof(uint64_t)};

	ctx_t* ctx = (ctx_t*)ctxmem;

	void* const_bufs[] = {&ctx->METHOD_GET, &ctx->METHOD_POST, &ctx->METHOD_HEAD, &ctx->RANGE_SEEK_SET, &ctx->RANGE_SEEK_END};

	memset(ctx, 0, sizeof(ctx_t));

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
	    ERROR_RETURN_LOG(int, "Cannot parse the options");

	if(ERROR_CODE(pipe_t) == (ctx->p_input = pipe_define("input", PIPE_INPUT, NULL)))
	    ERROR_RETURN_LOG(int, "Cannot define the input pipe");

	if(ERROR_CODE(pipe_t) == (ctx->p_protocol_data = pipe_define("protocol_data", PIPE_OUTPUT, "plumber/std_servlet/network/http/parser/v0/ProtocolData")))
	    ERROR_RETURN_LOG(int, "Cannot define the protocol data pipe");

	PSTD_TYPE_MODEL(type_model)
	{
		PSTD_TYPE_MODEL_FIELD(ctx->p_protocol_data, accept_encoding.token, ctx->a_accept_encoding),
		PSTD_TYPE_MODEL_FIELD(ctx->p_protocol_data, upgrade_target.token,  ctx->a_upgrade_target),
		PSTD_TYPE_MODEL_FIELD(ctx->p_protocol_data, error,                 ctx->a_error),
		PSTD_TYPE_MODEL_CONST(ctx->p_protocol_data, ERROR_NONE,            ctx->ERROR_NONE),
		PSTD_TYPE_MODEL_CONST(ctx->p_protocol_data, ERROR_BAD_REQ,         ctx->ERROR_BAD_REQ)
	};

	if(NULL == (ctx->type_model = PSTD_TYPE_MODEL_BATCH_INIT(type_model)))
	    ERROR_RETURN_LOG(int, "Cannot create type model for the servlet");

	if(ERROR_CODE(int) == routing_map_initialize(ctx->options.routing_map, ctx->type_model))
	    ERROR_RETURN_LOG(int, "Cannot initailize the routing map");

	if(ERROR_CODE(int) == proto_init())
	    ERROR_RETURN_LOG(int, "Cannot initialize libproto");

	for(i = 0; i < sizeof(const_sizes) / sizeof(const_sizes[0]); i ++)
	    if(_read_const_unsigned(const_names[i], const_bufs[i], const_sizes[i]) == ERROR_CODE(int))
	        ERROR_LOG_GOTO(ERR, "Cannot read constant for GET method");

	int rc = ERROR_CODE(int);

	rc = 0;
	goto RET;
ERR:
#ifdef LOG_ERROR_ENABLED
	LOG_ERROR("=========== libproto stack ============");
	const proto_err_t* stack = proto_err_stack();

	for(;stack != NULL; stack = stack->child)
	{
		char buf[1024];

		LOG_ERROR("%s", proto_err_str(stack, buf, sizeof(buf)));
	}
#endif

	proto_err_clear();
	LOG_ERROR("=========== end of libproto stack ============");

RET:
	if(ERROR_CODE(int) == proto_finalize())
	    ERROR_RETURN_LOG(int, "Cannot finalize libproto");

	return rc;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;
	if(ERROR_CODE(int) == options_free(&ctx->options))
	    rc = ERROR_CODE(int);

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
	    rc = ERROR_CODE(int);

	return rc;
}

static int _state_free(void* state)
{
	return parser_state_free((parser_state_t*)state);
}

static inline int _determine_routing(const ctx_t* ctx, const char* host, size_t host_len, const char* path, size_t path_len, routing_result_t* result)
{
	routing_state_t state;
	routing_state_init(&state, ctx->options.routing_map, result);

	size_t sz = routing_process_buffer(&state, host, host_len, 0);
	if(sz == ERROR_CODE(size_t))
	    ERROR_RETURN_LOG(int, "Cannot parse the host");

	if(!state.done)
	{

		sz = routing_process_buffer(&state, path, path_len, 1);
		if(ERROR_CODE(size_t) == sz)
		    ERROR_RETURN_LOG(int, "Cannot parse the path");
	}

	return state.done;
}

static int _exec(void* ctxmem)
{
	char _buffer[4096];
	char* buffer = NULL;
	size_t sz;
	int parser_done = 0, servlet_rc = ERROR_CODE(int);
	pstd_type_instance_t* type_inst = NULL;

	ctx_t* ctx = (ctx_t*)ctxmem;

	/* Before we start, we need to check if we have previously saved parser state */
	parser_state_t* state = NULL;
	if(ERROR_CODE(int) == pipe_cntl(ctx->p_input, PIPE_CNTL_POP_STATE, &state))
	    ERROR_RETURN_LOG(int, "Cannot pop the previously saved state");

	int new_state = 0;
	if(NULL == state)
	{
		if(NULL == (state = parser_state_new()))
		    ERROR_RETURN_LOG(int, "Cannot allocate memory for the new parser state");
		new_state = 1;
	}

	/* Ok, now we have a valid parser state and are going to parse the request */
	for(;;)
	{
		/* Before we actually started, we need to release the previously acquired internal buffer */
		size_t min_sz;

		if(buffer != _buffer && buffer != NULL && ERROR_CODE(int) == pipe_data_release_buf(ctx->p_input, buffer, sz))
		    ERROR_LOG_GOTO(ERR, "Cannot release the previously acquired internal buffer");

		/* Try to access the internal buffer before we actually call read, thus we can avoid copy */
		sz = sizeof(_buffer);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
		/* Clang has a ridiculous restriction on the qualifier cast. */
		int rc = pipe_data_get_buf(ctx->p_input, sizeof(_buffer), (void const**)&buffer, &min_sz, &sz);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
		if(ERROR_CODE(int) == rc)
		    ERROR_LOG_GOTO(ERR, "Cannot get the internal buffer");

		/* If the pipe cannot return the internal buffer, then we read the buffer */
		if(rc == 0)
		{
			buffer = _buffer;
			if(ERROR_CODE(size_t) == (sz = pipe_read(ctx->p_input, buffer, sizeof(buffer))))
			    ERROR_LOG_GOTO(ERR, "Cannot read request data from pipe");
		}

		/* At this time, if we still unable to get anything, we need to do something */
		if(sz == 0)
		{
			int eof_rc = pipe_eof(ctx->p_input);

			if(eof_rc == ERROR_CODE(int))
			    ERROR_LOG_GOTO(ERR, "Cannot determine if the pipe has more data");

			if(eof_rc)
			{
				state->keep_alive = 0;
				state->error = 1;
				if(state->empty) goto NORMAL_EXIT;

				/* Although the parser is still in processing state, we still need to move ahead */
				goto PARSER_DONE;
			}
			else
			{
				/* The piep is waiting for more data, thus we can save the state and exit */
				if(ERROR_CODE(int) == pipe_cntl(ctx->p_input, PIPE_CNTL_SET_FLAG, PIPE_PERSIST))
				    ERROR_LOG_GOTO(ERR, "Cannot set the pipe to persistent mode");

				if(ERROR_CODE(int) == pipe_cntl(ctx->p_input, PIPE_CNTL_PUSH_STATE, state, _state_free))
				    ERROR_LOG_GOTO(ERR, "Cannot push the parser state to the pipe");

				return 0;
			}
		}
		else
		{
			/* Otherwise we are able to parse something */
			size_t bytes_consumed = parser_process_next_buf(state, buffer, sz);

			if(ERROR_CODE(size_t) == bytes_consumed)
			    ERROR_LOG_GOTO(ERR, "Cannot parse the request");

			/* If the parser doesn't consume all the feed in data, the parsing is definitely finished */
			if(bytes_consumed < sz)
			    parser_done = 1;
			else if(ERROR_CODE(int) == (parser_done = parser_state_done(state)))
			    ERROR_LOG_GOTO(ERR, "Cannot check if the request is complete");

			/* If we are done, we need to move ahead */
			if(parser_done)
			{
				if(_buffer == buffer)
				{
					if(bytes_consumed < sz && ERROR_CODE(int) == pipe_cntl(ctx->p_input, PIPE_CNTL_EOM, buffer, bytes_consumed))
					    ERROR_LOG_GOTO(ERR, "Cannot unread the bytes");
				}
				else
				{
					if(ERROR_CODE(int) == pipe_data_release_buf(ctx->p_input, buffer, bytes_consumed))
					    ERROR_LOG_GOTO(ERR, "Cannot unread the buffer");
				}
				goto PARSER_DONE;
			}
		}
	}

PARSER_DONE:

	/* If we just compeleted the parsing stage */
	if(NULL == (type_inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model)))
	    ERROR_RETURN_LOG(int, "Cannot allocate memory for the type instance");

	routing_result_t result;

	if(state->error)
	{
		/* If the request is invalid we need to write a invalid message */
		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_error, ctx->ERROR_BAD_REQ))
		    ERROR_LOG_GOTO(ERR, "Cannot write the bad request flag to the protocol data structure");

		goto NORMAL_EXIT;
	}

	if(ERROR_CODE(int) == _determine_routing(ctx, state->host.value, state->host.length, state->path.value, state->path.length, &result))
	    ERROR_LOG_GOTO(ERR, "Cannot dispose the parser state");

	if(state->keep_alive && ERROR_CODE(int) == pipe_cntl(ctx->p_input, PIPE_CNTL_SET_FLAG, PIPE_PERSIST))
	    ERROR_LOG_GOTO(ERR, "Cannot set the persist flag");

	/* If we reached here, it means we've got a good http request */
	uint32_t method_code = ERROR_CODE(uint32_t);
	switch(state->method)
	{
		case PARSER_METHOD_GET:
		    method_code = ctx->METHOD_GET;
		    break;
		case PARSER_METHOD_POST:
		    method_code = ctx->METHOD_POST;
		    break;
		case PARSER_METHOD_HEAD:
		    method_code = ctx->METHOD_HEAD;
		    break;
		default:
		    ERROR_LOG_GOTO(ERR, "Code bug: Invalid method");
	}

	if(state->accept_encoding.value != NULL &&
	    ERROR_CODE(int) == pstd_string_transfer_commit_write(type_inst, ctx->a_accept_encoding,
	                                                         state->accept_encoding.value,
	                                                         state->accept_encoding.length))
	    ERROR_LOG_GOTO(ERR, "Cannot write the accept encdoding to the protocol data buffer");
	state->accept_encoding.value = NULL;

	if(result.should_upgrade)
	{
		/* Then we need to determine if we should upgrade */
		const char* modpath = NULL;
		static const char tcp_prefix[] = "pipe.tcp.";

		if(ERROR_CODE(int) == pipe_cntl(ctx->p_input, PIPE_CNTL_MODPATH, &modpath))
		    ERROR_RETURN_LOG(int, "Cannot get the modeule path for the IO module");

		if(strncmp(modpath, tcp_prefix, sizeof(tcp_prefix) - 1) == 0)
		{
			/* If we got a plain HTTP requeset */
			pstd_string_t* target_obj = pstd_string_new(32);
			if(NULL == target_obj)
			    ERROR_LOG_GOTO(ERR, "Cannot create target URL object");

			if(result.https_url_base == NULL)
			{
				if(ERROR_CODE(size_t) == pstd_string_printf(target_obj, "https://%s%s", state->host.value, state->path.value))
				    ERROR_LOG_GOTO(UPGRADE_ERR, "Cannot write scheme to the URL object");
			}
			else
			{
				if(ERROR_CODE(size_t) == pstd_string_printf(target_obj, "%s%s", result.https_url_base, state->path.value))
				    ERROR_LOG_GOTO(UPGRADE_ERR, "Cannot write scheme to the URL object");
			}

			scope_token_t tok = pstd_string_commit(target_obj);
			if(ERROR_CODE(scope_token_t) == tok)
			    ERROR_LOG_GOTO(UPGRADE_ERR, "Cannot commit the target URL to RLS");

			target_obj = NULL;

			if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, ctx->a_upgrade_target, tok))
			    ERROR_LOG_GOTO(UPGRADE_ERR, "Cannot write the token to the pipe");

			goto NORMAL_EXIT;
UPGRADE_ERR:
			if(NULL != target_obj)
			    pstd_string_free(target_obj);

			goto ERR;
		}
	}


	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, result.out->a_method, method_code))
	    ERROR_LOG_GOTO(ERR, "Cannot write method to the result pipe");

	if(ERROR_CODE(int) == pstd_string_transfer_commit_write(type_inst, result.out->a_host, state->host.value, state->host.length))
	    ERROR_LOG_GOTO(ERR, "Cannot write hostname to the result pipe");
	state->host.value = NULL;

	if(ERROR_CODE(int) == pstd_string_create_commit_write_sz(type_inst, result.out->a_base_url, result.url_base + result.host_len, result.url_base_len))
	    ERROR_LOG_GOTO(ERR, "Cannot write the base URL base to result pipe");

	if(ERROR_CODE(int) == pstd_string_transfer_commit_write_range(type_inst, result.out->a_rel_url, state->path.value, result.url_base_len, state->path.length))
	    ERROR_LOG_GOTO(ERR, "Cannot write the relative URL to the result pipe");
	state->path.value = NULL;

	if(state->query.value != NULL && ERROR_CODE(int) == pstd_string_transfer_commit_write(type_inst, result.out->a_query_param, state->query.value, state->query.length))
	    ERROR_LOG_GOTO(ERR, "Cannot write the query param to the result pipe");
	state->query.value = NULL;

	if(state->body.value != NULL && ERROR_CODE(int) == pstd_string_transfer_commit_write(type_inst, result.out->a_body, state->body.value, state->body.length))
	    ERROR_LOG_GOTO(ERR, "Cannot write the data body to the result pipe");
	state->body.value = NULL;

	uint64_t begin = ctx->RANGE_SEEK_SET;
	uint64_t end   = ctx->RANGE_SEEK_END;

	if(state->has_range)
	{
		if(state->range_begin != ERROR_CODE(uint64_t)) begin = state->range_begin;
		if(state->range_end != ERROR_CODE(uint64_t)) end = state->range_end + 1;
	}

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, result.out->a_range_begin, begin))
	    ERROR_LOG_GOTO(ERR, "Cannot write the range begin to the result pipe");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, result.out->a_range_end, end))
	    ERROR_LOG_GOTO(ERR, "Cannot write the range end to the result pipe");


NORMAL_EXIT:
	servlet_rc = 0;
ERR:
	if(NULL != type_inst && ERROR_CODE(int) == pstd_type_instance_free(type_inst))
	    servlet_rc = ERROR_CODE(int);

	if(new_state && NULL != state && ERROR_CODE(int) == parser_state_free(state))
	    servlet_rc = ERROR_CODE(int);
	return servlet_rc;
}

SERVLET_DEF = {
	.desc    = "The HTTP Request Parser",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};
