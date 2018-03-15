/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <parser.h>

/**
 * @brief The HTTP parser state codes
 **/
typedef enum {
	_STATE_ERROR = -1,      /*!< We are unable to parse the request */
	_STATE_INIT,            /*!< The state when we initialize the request parser */
	_STATE_METHOD_GET,      /*!< The method phrase is determined and we need to match the method */
	_STATE_METHOD_HEAD,     /*!< The method phrase is determined and we need to match the method */
	_STATE_METHOD_POST,     /*!< The method phrase is determined and we need to match the method */
	_STATE_METHOD_PATH_SEP, /*!< The state for we are in the middle of method phrase and server path */
	_STATE_URI,             /*!< We are parsing the URI */
	_STATE_URI_SCHEME,      /*!< We are parsing the URL scheme (Based on the RFC the URL can be a full URI*/
	_STATE_URI_SCHEME_SEP,  /*!< We want to strip scheme://xxx */
	_STATE_URI_HOST,        /*!< The host name we parsed in the URI */
	_STATE_URI_PATH,        /*!< The path we are parsing */
	_STATE_URI_QUERY,       /*!< We are parsing the query parameter */
	_STATE_URI_VERSION_SEP, /*!< The space between URI and HTTP version identifer */
	_STATE_VERSION,         /*!< The HTTP version */
	_STATE_REQ_LINE_END,    /*!< We are parsing the \r\n */
	_STATE_FIELD_NAME,      /*!< We are parsing a field name */
	_STATE_FIELD_KV_SEP,    /*!< We are parsing the field name - field value delimitor */
	_STATE_FIELD_VALUE,     /*!< We are parsing the content of the value */
	_STATE_FIELD_LINE_END,  /*!< We are parsing the end of the field line */
	_STATE_FIELD_NOT_INST,  /*!< The field we are not interested */
	_STATE_BODY_DATA,       /*!< We are parsing the data */
	_STATE_BODY_END,        /*!< We are parsing the body end */
	_STATE_DONE,            /*!< We have parsed everything */
	_STATE_COUNT            /*!< The number of states */
} _state_code_t;

/**
 * @brief The state type
 **/
typedef enum {
	_STATE_TYPE_GENERIC = 0,   /*!< Indicates this is not a predefined state */
	_STATE_TYPE_LITER,         /*!< Indicates in this state we need to match a literal constant, if failed goto error */
	_STATE_TYPE_COPY,          /*!< Indicates in this state we want to copy the data into a buffer, util we see a char */
	_STATE_TYPE_WS,            /*!< Indicates in this state we want to trip the white spaces */
	_STATE_TYPE_IGNORE         /*!< We want to ignore the data until we see the char */
} _state_type_t;

/**
 * @brief The description of the state
 **/
typedef struct {
	_state_type_t type;     /*!< The type of the state */
	_state_type_t next;     /*!< The next state */
	union {
		const char*  liter;    /*!< The literal we want to match */
		struct {
			char     term;  /*!< The ending string we want to terminate the copy */
			int32_t  lim;   /*!< The size limit of the buffer */
		}         copy;     /*!< The copy param */
		int32_t   ws;       /*!< How many white space at least we want to see */
		char      ignore;   /*!< The char we want to ends the ignore state */
	} param;                /*!< The parameter of the state */
} _state_desc_t;


#define _GENERIC(name)            [_STATE_##name] = {.type = _STATE_TYPE_GENERIC}
#define _LITERAL(name, ns, lit)   [_STATE_##name] = {.type = _STATE_TYPE_LITER, .next = _STATE_##ns, .param = {.liter = lit}}
#define _COPY(name, ns, tr, sz)   [_STATE_##name] = {.type = _STATE_TYPE_COPY,  .next = _STATE_##ns, .param = {.copy = {.term = tr, .lim = sz}}}
#define _WS(name, ns, n)          [_STATE_##name] = {.type = _STATE_TYPE_WS, .next = _STATE_##ns, .param = {.ws = n}}
#define _IGNORE(name, ns, tr)     [_STATE_##name] = {.type = _STATE_TYPE_IGNORE, .next = _STATE_##ns, .param = {.ignore = tr}}

/**
 * @brief The state description table
 **/
static _state_desc_t _state_info[_STATE_COUNT] = {
	_GENERIC(INIT),
	_LITERAL(METHOD_GET,       METHOD_PATH_SEP, "ET "       ),
	_LITERAL(METHOD_HEAD,      METHOD_PATH_SEP, "EAD "      ),
	_LITERAL(METHOD_POST,      METHOD_PATH_SEP, "OST "      ),
	_WS     (METHOD_PATH_SEP,  URI,              0          ),
	_GENERIC(URI                                            ),
	_IGNORE (URI_SCHEME,       URI_SCHEME_SEP,  ':'         ),
	_LITERAL(URI_SCHEME_SEP,   URI_HOST,        "//"        ),
	_COPY   (URI_HOST,         URI_PATH,        '/',      64),
	_GENERIC(URI_PATH                                       ),
	_COPY   (URI_QUERY,        URI_VERSION_SEP, ' ',    2048),
	_WS     (URI_VERSION_SEP,  VERSION,          0           ),
	_IGNORE (VERSION,          REQ_LINE_END,    '\r'         ),
	_LITERAL(REQ_LINE_END,     FIELD_NAME,      "\n"         ),
	_GENERIC(FIELD_NAME                                      ),
	_GENERIC(FIELD_KV_SEP                                    ),
	_GENERIC(FIELD_VALUE                                     ),
	_IGNORE (FIELD_NOT_INST,   FIELD_LINE_END,  '\r'         ),
	_LITERAL(FIELD_LINE_END,   FIELD_NAME,      "\r\n"       ),
	_GENERIC(BODY_DATA                                       ),
	_GENERIC(DONE                                            )
};

typedef enum {
	_FIELD_NAME_HOST,              /*!< Host */
	_FIELD_NAME_ACCEPT_ENCODING,   /*!< Accept encoding */
	_FIELD_NAME_RANGE,             /*!< Range */
	_FIELD_NAME_CONN,              /*!< Connection */
	_FIELD_NAME_CL,                /*!< Content Length */
	_FIELD_NAME_N_DETERMINED,      /*!< How many headers we really care about */ 
	_FIELD_NAME_UNKNOWN,           /*!< We are about to start */
	_FIELD_NAME_CONN_OR_CL,        /*!< Connection or Content-Length */
	_FIELD_NAME_GENERIC            /*!< A generic header we don't care about */
} _field_name_state_t;

static const char const* _field_name[] = {
	[_FIELD_NAME_HOST]              = "host",
	[_FIELD_NAME_ACCEPT_ENCODING]   = "accept-encoding",
	[_FIELD_NAME_RANGE]             = "range",
	[_FIELD_NAME_CONN]              = "connection",
	[_FIELD_NAME_CL]                = "content-length"
};


/**
 * @brief The internal state 
 **/
typedef struct {
	_state_code_t    code;       /*!< The state code */
	int              sub_state;  /*!< The substate */
	_field_name_state_t fn_state;/*!< The field name state */
	parser_string_t* buffer;     /*!< The string buffer */
} _state_t;

static inline int _process_next_buf(parser_state_t* state, const char* data, size_t size)
{
	_state_t* internal = (_state_t*)state->internal_state;
	
	while(size > 0 && internal->code != _STATE_DONE && internal->code != _STATE_ERROR)
	{
		const _state_desc_t* si = _state_info + internal->code;
		switch(si->type)
		{
			case _STATE_TYPE_LITER:
			{
				const char* to_match = si->param.liter;
				for(;size > 0 && to_match[internal->sub_state]; size--, internal->sub_state ++, data ++)
					if(to_match[internal->sub_state] != data[0])
						goto ERROR;
				if(to_match[internal->sub_state] == 0) 
					goto TRANSIT;
				break;
			}
			case _STATE_TYPE_COPY:
			{
				for(;size > 0 && data[0] != si->param.copy.term; internal->buffer->length ++, data ++, size --)
					if(si->param.copy.lim < internal->sub_state)
						goto ERROR;
					else 
						internal->buffer->value[internal->buffer->length] = data[0];
				if(data[0] == si->param.copy.term)
				{
					data ++, size --;
					internal->buffer->value[internal->buffer->length] = 0;
					goto TRANSIT;
				}
				break;
			}
			case _STATE_TYPE_IGNORE:
			{
				for(; size > 0 && data[0] != si->param.ignore; data ++, size --);
				if(data[0] != si->param.ignore)
				{
					data ++, size --;
					goto TRANSIT;
				}
				break;
			}
			case _STATE_TYPE_WS:
			{
				for(;size > 0 && (data[0] == ' ' || data[0] == '\t'); data ++, size --, internal->sub_state ++);
				if(data[0] != ' ' && data[0] != '\t')
				{
					if(internal->sub_state >= si->param.ws)
						goto TRANSIT;
					else
						goto ERROR;
				}
				break;
			}
			case _STATE_TYPE_GENERIC:
			{
				switch(internal->code)
				{
					case _STATE_INIT:
					{
						if(data[0] == 'G') 
							internal->code = _STATE_METHOD_GET;
						else if(data[0] == 'P')
							internal->code = _STATE_METHOD_POST;
						else if(data[0] == 'H')
							internal->code = _STATE_METHOD_HEAD;
						else goto ERROR;
						size --, data ++;
						goto GENERIC_TRANS;
					}
					case _STATE_URI:
					{
						if(data[0] != '/')
						{
							internal->code = _STATE_URI_SCHEME;
							size --, data ++;
							goto GENERIC_TRANS;
						}

						internal->code = _STATE_URI_PATH;
						goto GENERIC_TRANS;
					}
					case _STATE_FIELD_NAME:
					{
						for(;size > 0;)
						{
							switch(internal->fn_state)
							{
								case _FIELD_NAME_UNKNOWN:
									if(data[0] == 'h' || data[0] == 'H')
										internal->fn_state = _FIELD_NAME_HOST;
									else if(data[0] == 'r' || data[0] == 'R')
										internal->fn_state = _FIELD_NAME_RANGE;
									else if(data[0] == 'a' || data[0] == 'A')
										internal->fn_state = _FIELD_NAME_ACCEPT_ENCODING;
									else if(data[0] == 'c' || data[0] == 'C')
										internal->fn_state = _FIELD_NAME_CONN_OR_CL;
									else if(data[0] == '\r')
									{
										internal->code = _STATE_BODY_DATA;
										goto GENERIC_TRANS;
									}
									else
									{
										internal->fn_state = _FIELD_NAME_GENERIC;
										internal->code = _STATE_FIELD_NOT_INST;
										goto GENERIC_TRANS;
									}
									data ++, size --, internal->sub_state = 1;
									break;
								case _FIELD_NAME_CONN:
								case _FIELD_NAME_ACCEPT_ENCODING:
								case _FIELD_NAME_RANGE:
								case _FIELD_NAME_CL:
									for(;size > 0 && _field_name[internal->fn_state][internal->sub_state]; internal->sub_state ++, size --, data ++)
										if(_field_name[internal->fn_state][internal->sub_state] != (data[0] | (0x20 & (data[0] >> 1))))
										{
											internal->fn_state = _FIELD_NAME_GENERIC;
											internal->code = _STATE_FIELD_NOT_INST;
											goto GENERIC_TRANS;
										}
									if(_field_name[internal->fn_state][internal->sub_state] == 0)
									{
										internal->code = _STATE_FIELD_KV_SEP;
										goto GENERIC_TRANS;
									}
									break;
								case _FIELD_NAME_CONN_OR_CL:
									for(;size > 0 && _field_name[_FIELD_NAME_CONN][internal->sub_state] == _field_name[_FIELD_NAME_CL][internal->sub_state]; 
										data ++, size --, internal->sub_state ++)
										if(_field_name[_FIELD_NAME_CONN][internal->sub_state] != (data[0] | (0x20 & (data[0] >> 1))))
										{
											internal->fn_state = _FIELD_NAME_GENERIC;
											internal->code = _STATE_FIELD_NOT_INST;
											goto GENERIC_TRANS;
										}
									if(data[0] == 't' || data[0] == 'T')
										internal->fn_state = _FIELD_NAME_CL;
									else if(data[0] == 'n' || data[0] == 'N')
										internal->fn_state = _FIELD_NAME_CONN;
									else
									{
										internal->code = _STATE_FIELD_NOT_INST;
										internal->fn_state = _FIELD_NAME_GENERIC;
										goto GENERIC_TRANS;
									}
								default:
									(void)0;
							}
						}
						break;
					}
					case _STATE_FIELD_KV_SEP:
					{
						switch(internal->sub_state)
						{
							case 0:
							case 2:
								for(;(data[0] == '\t' || data[0] == ' '); data ++, size --);
								if(internal->sub_state == 0)
								{
									if(data[0] != '\t' || data[0] != ' ')
									{	
										internal->sub_state = 1;
									}
								}
								else if(internal->sub_state == 2)
								{
									internal->code = _STATE_FIELD_VALUE;
									goto TRANSIT;
								}
								break;
							case 1:
								if(data[0] != ':')  goto ERROR;
								internal->sub_state = 2;
						}
						break;
					}
					case _STATE_FIELD_VALUE:
					{
						/* TODO */
						break;
					}
					case _STATE_BODY_DATA:
					{
						break;
					}
					case _STATE_DONE:
					{
						break;
					}
					default:
					(void)0;
				}
			}
		}
		continue;
TRANSIT:
		internal->code = si->next;
GENERIC_TRANS:
		internal->sub_state = 0;
		if(_state_info[internal->code].type == _STATE_TYPE_COPY)
		{
			if(internal->code == _STATE_URI_HOST)
				internal->buffer = &state->host;
			else if(internal->code == _STATE_URI_QUERY)
				internal->buffer = &state->query;
			if(NULL == (internal->buffer->value = (char*)malloc((size_t)_state_info[internal->code].param.copy.lim + 1)))
			{
				LOG_ERROR_ERRNO("Cannot allocate memory for the buffer");
				goto ERROR;
			}
			internal->buffer->length = 0;
		}
		if(internal->code == _STATE_FIELD_NAME)
			internal->fn_state = _FIELD_NAME_UNKNOWN;
		continue;
ERROR:
		internal->code = _STATE_ERROR;
		state->error = 1;
		return 0;
	}
}

parser_state_t* parser_state_new()
{
	(void)_state_info;
	parser_state_t* ret = (parser_state_t*)calloc(sizeof(parser_state_t) + sizeof(_state_t), 1);

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the state");

	ret->empty = 1;

	return ret;
}
