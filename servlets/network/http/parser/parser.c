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
	_STATE_FIELD_NAME_INIT, /*!< We are parsing a field name */
	_STATE_FIELD_NAME_ACCEPT_ENC,   /*!< We are parsering the Accept-Encoding field name */
	_STATE_FIELD_NAME_RANGE,        /*!< We are parsing the Range field name */
	_STATE_FIELD_NAME_HOST,         /*!< We are parsing the Host field name */
	_STATE_FIELD_NAME_CONTENT_LEN,  /*!< We are parsing the content-length name */
	_STATE_FIELD_NAME_CONNECT,      /*!< We are parsing the connection field name */
	_STATE_FIELD_KV_SEP,    /*!< We are parsing the field name - field value delimitor */
	_STATE_FIELD_VALUE,     /*!< We are parsing the content of the value */
	_STATE_FIELD_VAL_HOST,
	_STATE_FIELD_VAL_ACCEPT_ENC,
	_STATE_FIELD_VAL_RANGE,
	_STATE_FIELD_LINE_END,  /*!< We are parsing the end of the field line */
	_STATE_FIELD_NOT_INST,  /*!< The field we are not interested */
	_STATE_BODY_BEGIN,      /*!< We are reading the last \r\n */
	_STATE_BODY_DATA,       /*!< We are parsing the data */
	_STATE_BODY_END,        /*!< We are parsing the body end */
	_STATE_DONE,            /*!< We have parsed everything */
	_STATE_COUNT,           /*!< The number of states */
	_STATE_PENDING          /*!< We are waiting for finalize the state transition */
} _state_code_t;

/**
 * @brief The state type
 **/
typedef enum {
	_STATE_TYPE_GENERIC = 0,   /*!< Indicates this is not a predefined state */
	_STATE_TYPE_LITER,         /*!< Indicates in this state we need to match a literal constant, if failed goto error */
	_STATE_TYPE_LITER_IC,      /*!< Liternal constant but ignore the case */
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
		struct {
			const char* str;/*!< The literal we want to match */
			size_t      size; /*!< The size of the string */
		} liter, liter_ic;   /*!< The literal and literal case insensitive */ 
		struct {
			uint32_t off;   /*!< The offset for the buffer */
			char     term;  /*!< The ending string we want to terminate the copy */
			int32_t  lim;   /*!< The size limit of the buffer */
		}         copy;     /*!< The copy param */
		int32_t   ws;       /*!< How many white space at least we want to see */
		char      ignore;   /*!< The char we want to ends the ignore state */
	} param;                /*!< The parameter of the state */
} _state_desc_t;


#define _GENERIC(name)                 [_STATE_##name] = {.type = _STATE_TYPE_GENERIC}
#define _LITERAL(name, ns, lit)        [_STATE_##name] = {.type = _STATE_TYPE_LITER, .next = _STATE_##ns, .param = {.liter = {.str = lit, .size = sizeof(lit) - 1}}}
#define _LITERAL_IC(name, ns, lit)     [_STATE_##name] = {.type = _STATE_TYPE_LITER_IC, .next = _STATE_##ns, .param = {.liter ={ .str = lit, .size = sizeof(lit) - 1}}}
#define _COPY(name, ns, tr, sz, ofs)   [_STATE_##name] = {\
	.type = _STATE_TYPE_COPY,  \
	.next = _STATE_##ns, \
	.param = {\
		.copy = {\
			.term = tr, \
			.lim = sz, \
			.off = (uint32_t)(uint64_t)&((parser_state_t*)NULL)->ofs\
		}\
	}\
}
#define _WS(name, ns, n)               [_STATE_##name] = {.type = _STATE_TYPE_WS, .next = _STATE_##ns, .param = {.ws = n}}
#define _IGNORE(name, ns, tr)          [_STATE_##name] = {.type = _STATE_TYPE_IGNORE, .next = _STATE_##ns, .param = {.ignore = tr}}

/**
 * @brief The state description table
 **/
static _state_desc_t _state_info[_STATE_COUNT] = {
	_GENERIC(INIT),                                          /* In this state we need to determine which is the next state and set the method code*/
	_LITERAL(METHOD_GET, METHOD_PATH_SEP,  "ET "),           /* We are going to match GET */ 
	_LITERAL(METHOD_HEAD, METHOD_PATH_SEP, "EAD "),          /* We are going to match HEAD */ 
	_LITERAL(METHOD_POST, METHOD_PATH_SEP, "OST "),          /* We are going to match POST */
	_WS(METHOD_PATH_SEP, URI, 0),                            /* Strip the extra white space between method and URI */
	_GENERIC(URI),                                           /* In this state we need to determine if we got a full URI or relative path */
	_IGNORE(URI_SCHEME, URI_SCHEME_SEP, ':'),                /* We just ignore whatever scheme it specify */
	_LITERAL(URI_SCHEME_SEP, URI_HOST, "://"),               /* We should expect the delimitor for the scheme and host name */
	_COPY(URI_HOST, URI_PATH, '/', 64, host),                /* Then we have the host name to copy to the buffer */
	_GENERIC(URI_PATH),                                      /* In this state we need to copy the path to buffer and determine if we have query parameter */
	_COPY(URI_QUERY, URI_VERSION_SEP, ' ', 2048, query),     /* We simply copy the data to the query param */
	_WS(URI_VERSION_SEP, VERSION, 1),                        /* Strip at least one space after the URI */
	_IGNORE(VERSION, REQ_LINE_END, '\r'),                    /* For whatever HTTP version, we don't actually care about this */
	_LITERAL(REQ_LINE_END, FIELD_NAME_INIT, "\r\n"),              /* Finally we need a sign to complete the first line */
	_GENERIC(FIELD_NAME_INIT),                               /* In this state we need to determine which state we are goging to parse */
	_LITERAL_IC(FIELD_NAME_ACCEPT_ENC, FIELD_KV_SEP, "ccept-encoding:"),  /* We are matching the accept-encoding field */
	_LITERAL_IC(FIELD_NAME_RANGE, FIELD_KV_SEP, "ange:"),                 /* We are matching range field */
	_LITERAL_IC(FIELD_NAME_HOST, FIELD_KV_SEP, "ost:"),                   /* We are matching host field */
	_LITERAL_IC(FIELD_NAME_CONNECT, FIELD_KV_SEP, "nection:"),            /* We are matching connection field */
	_LITERAL_IC(FIELD_NAME_CONTENT_LEN, FIELD_KV_SEP, "tent-length:"),    /* We are matching content-length field */
	_WS(FIELD_KV_SEP, FIELD_VALUE, 0),                       /* In this state we handle each of the state differently */
	_GENERIC(FIELD_VALUE),                                   /* All the non-copy header */
	_COPY(FIELD_VAL_HOST, FIELD_LINE_END, '\r', 64, host),       /* All the non-copy header */
	_COPY(FIELD_VAL_ACCEPT_ENC, FIELD_LINE_END, '\r', 64, accept_encoding), /* All the non-copy header */
	_COPY(FIELD_VAL_RANGE, FIELD_LINE_END, '\r', 64, range_text), /* All the non-copy header */
	_IGNORE(FIELD_NOT_INST, FIELD_LINE_END, '\r'),           /* We are going to ignore this field */
	_LITERAL(FIELD_LINE_END, FIELD_NAME_INIT, "\r\n"),       /* We should check if we really come to the end of the field line */
	_LITERAL(BODY_BEGIN, BODY_DATA, "\r\n"),                 /* We expect the last \r\n */
	_GENERIC(BODY_DATA),                                     /* Read all body data */
	_GENERIC(DONE)                                           /* Finally we are done */
};

typedef enum {
	_FIELD_NAME_UNKNOWN,
	_FIELD_NAME_CON_OR_CL,
	_FIELD_NAME_N_DETERMINED,      /*!< Number of determined */
	_FIELD_NAME_HOST,              /*!< Host */
	_FIELD_NAME_ACCEPT_ENCODING,   /*!< Accept encoding */
	_FIELD_NAME_RANGE,             /*!< Range */
	_FIELD_NAME_CONN,              /*!< Connection */
	_FIELD_NAME_CL,                /*!< Content Length */
} _field_name_state_t;

/**
 * @brief The internal state 
 **/
typedef struct {
	_state_code_t       code;       /*!< The state code */
	int                 sub_state;  /*!< The substate */
	_field_name_state_t fn_state;/*!< The field name state */
	parser_string_t*    buffer;     /*!< The string buffer */
	size_t              buffer_cap; /*!< The buffer capacity */
} _state_t;

static inline void _transite_state(parser_state_t* state, _state_code_t next)
{
	_state_t* internal = (_state_t*)state->internal_state;
	internal->code = _STATE_PENDING | next;
}

static inline int _init_buffer(parser_state_t* state, uintptr_t offset, size_t limit)
{
	_state_t* internal = (_state_t*)state->internal_state;
	internal->buffer = (parser_string_t*)((uintptr_t)state + offset);
	size_t init_size = 64;
	if(limit + 1 < init_size)
		init_size = limit + 1;
	
	if(NULL == (internal->buffer->value = (char*)malloc(init_size)))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the copy buffer");
	internal->buffer->length = 0;
	internal->buffer_cap = init_size;
	return 0;
}

static inline int _ensure_buffer(parser_state_t* state, size_t new_bytes, size_t limit)
{
	_state_t* internal = (_state_t*)state->internal_state;
	if(internal->buffer->length + new_bytes + 1  > internal->buffer_cap)
	{
		size_t new_size = internal->buffer_cap;
		for(;new_size < internal->buffer->length + new_bytes + 1; new_size <<= 1);
		if(new_size < limit + 1)
		{
			_transite_state(state, _STATE_ERROR);
			return 0;
		}
		char* new_buf = (char*)realloc(internal->buffer->value, new_size);
		if(NULL == new_buf)
			return ERROR_CODE(int);

		internal->buffer->value = new_buf;
		internal->buffer_cap = new_size;
	}

	return 1;
}

static inline const char* _copy(parser_state_t* state, const char* data, const char* end)
{
	_state_t* internal = (_state_t*)state->internal_state;
	const _state_desc_t* si = _state_info + internal->code;

	if(internal->code & _STATE_PENDING)
	{
		if(ERROR_CODE(int) == _init_buffer(state, (uintptr_t)si->param.copy.off, (size_t)si->param.copy.lim))
			return NULL;
		internal->code ^= _STATE_PENDING;
	}

	const char* termpos = data;
	int last = 0;

	termpos = memchr(data, si->param.copy.term, (size_t)(end - data));

	if(NULL == termpos) 
		termpos = end;
	else 
		last = 1;

	int ensure_rc = _ensure_buffer(state, (size_t)(termpos - data), (size_t)si->param.copy.lim);
	if(ensure_rc == 0) return data;
	if(ensure_rc == ERROR_CODE(int)) return NULL;

	if(internal->buffer->length + (size_t)(termpos - data) + 1> internal->buffer_cap)

	memcpy(internal->buffer->value + internal->buffer->length, data, (size_t)(termpos - data));
	internal->buffer->length += (size_t)(termpos - data);

	if(last) 
	{
		internal->buffer->value[internal->buffer->length] = 0;
		_transite_state(state, si->next);
	}

	return termpos;
}

static inline const char* _uri_path(parser_state_t* state, const char* data, const char* end)
{
	_state_t* internal = (_state_t*)state->internal_state;

	if(internal->code & _STATE_PENDING)
	{
		if(ERROR_CODE(int) == _init_buffer(state, (uintptr_t)(&((parser_state_t*)NULL)->path), 2048))
			return NULL;
		internal->code ^= _STATE_PENDING;
	}

	for(;data < end && data[0] != '&' && data[0] != ' '; data ++)
	{
		int ensure_rc = _ensure_buffer(state, 1, 2048);
		if(ensure_rc == 0) return data;
		if(ensure_rc == ERROR_CODE(int)) return NULL;
		internal->buffer->value[internal->buffer->length++] = data[0];
	}

	if(data < end && data[0] == '&')
	{
		internal->buffer->value[internal->buffer->length] = 0;
		_transite_state(state, _STATE_URI_QUERY);
		return data + 1;
	}

	if(data < end && data[0] == ' ')
	{
		internal->buffer->value[internal->buffer->length] = 0;
		_transite_state(state, _STATE_VERSION);
		return data + 1;
	}

	return data;
}

static inline const char* _ignore(parser_state_t* state, const char* data, const char* end)
{
	_state_t* internal = (_state_t*)state->internal_state;
	const _state_desc_t* si = _state_info + internal->code;
	
	data = memchr(data, si->param.ignore, (size_t)(end - data));

	return data ? data : end;
}

static inline const char* _ws(parser_state_t* state, const char* data, const char* end)
{
	_state_t* internal = (_state_t*)state->internal_state;
	const _state_desc_t* si = _state_info + internal->code;

	if(internal->code & _STATE_PENDING)
	{
		internal->sub_state = si->param.ws;
		internal->code ^= _STATE_PENDING;
	}

	for(;data < end && (data[0] == ' ' || data[0] == '\t'); data ++)
		if(internal->sub_state > 0)
			internal->sub_state --;
	if(data < end && data[0] != ' ' && data[0] != '\t')
	{
		if(internal->sub_state > 0)
			_transite_state(state, _STATE_ERROR);
		else
			_transite_state(state, si->next);
	}

	return data;
}

static inline const char* _literal(parser_state_t* state, const char* data, const char* end)
{
	_state_t* internal = (_state_t*)state->internal_state;
	const _state_desc_t* si = _state_info + internal->code;
	
	if(internal->code & _STATE_PENDING)
	{
		internal->sub_state = 0;
		internal->code ^= _STATE_PENDING;
	}

	const char* to_match = si->param.liter.str;

	for(;data < end && to_match[internal->sub_state] != 0; data ++, internal->sub_state ++)
		if(to_match[internal->sub_state] != data[0])
		{
			_transite_state(state, _STATE_ERROR);
			return data;
		}
	if(to_match[internal->sub_state] == 0) 
		_transite_state(state, si->next);

	return data;
}

static inline const char* _literal_ci(parser_state_t* state, const char* data, const char* end)
{
	_state_t* internal = (_state_t*)state->internal_state;
	const _state_desc_t* si = _state_info + internal->code;
	
	if(internal->code & _STATE_PENDING)
	{
		internal->sub_state = 0;
		internal->code ^= _STATE_PENDING;
	}

	const char* to_match = si->param.liter.str;

	for(;data < end && to_match[internal->sub_state] != 0; data ++, internal->sub_state ++)
	{
		char ch = data[0];
		if(ch >= 'A' && ch <= 'Z')
			ch |= 0x20;
		if(to_match[internal->sub_state] != ch)
		{
			_transite_state(state, _STATE_ERROR);
			return data;
		}
	}
	if(to_match[internal->sub_state] == 0) 
		_transite_state(state, si->next);

	return data;
}

static inline const char* _init(parser_state_t* state, const char* data, const char* end)
{
	(void)end;
	_state_code_t next = _STATE_ERROR;

	switch(data[0])
	{
		case 'G':
			next = _STATE_METHOD_GET;
			state->method = PARSER_METHOD_GET;
			break;
		case 'P':
			/* TODO: We also needs to handle PUT method later */
			next = _STATE_METHOD_POST;
			state->method = PARSER_METHOD_POST;
			break;
		case 'H':
			next = _STATE_METHOD_HEAD;
			state->method = PARSER_METHOD_HEAD;
			break;
	}

	_transite_state(state, next);

	return data + 1;
}

static inline const char* _uri(parser_state_t* state, const char* data, const char* end)
{
	(void)end;

	if(data[0] == '/')
		_transite_state(state, _STATE_URI_PATH);
	else if(data[0] == 'h')
		_transite_state(state, _STATE_URI_SCHEME);
	else
		_transite_state(state, _STATE_ERROR);

	return data;
}

static inline const char* _field_value(parser_state_t* state, const char* data, const char* end)
{
	(void)end;
	_state_t* internal = (_state_t*)state->internal_state;

	switch(internal->fn_state)
	{
		case _FIELD_NAME_HOST:
			if(state->host.value != NULL)
			{
				_transite_state(state, _STATE_FIELD_NOT_INST);
				return data;
			}
			else
			{
				_transite_state(state, _STATE_FIELD_VAL_HOST);
				return data;
			}
			break;
		case _FIELD_NAME_ACCEPT_ENCODING:
			_transite_state(state, _STATE_FIELD_VAL_ACCEPT_ENC);
			return data;
		case _FIELD_NAME_RANGE:
			_transite_state(state, _STATE_FIELD_VAL_RANGE);
			return data;
		/* TODO: content-length, connection */
		default:
			_transite_state(state, _STATE_FIELD_VAL_HOST);
			return data;
	}


}

static inline const char* _field_name_init(parser_state_t* state, const char* data, const char* end)
{
	_state_t* internal = (_state_t*)state->internal_state;

	if(internal->code & _STATE_PENDING)
	{
		internal->fn_state = _FIELD_NAME_UNKNOWN;
		internal->code ^= _STATE_PENDING;
	}

	if(internal->fn_state == _FIELD_NAME_UNKNOWN)
	{
		if(data[0] == 'r' || data[0] == 'R')
		{
			_transite_state(state, _STATE_FIELD_NAME_RANGE);
			internal->fn_state = _FIELD_NAME_RANGE;
			return data + 1;
		}
		else if(data[0] == 'a' || data[0] == 'A')
		{
			_transite_state(state, _STATE_FIELD_NAME_ACCEPT_ENC);
			internal->fn_state = _FIELD_NAME_ACCEPT_ENCODING;
			return data + 1;
		}
		else if(data[0] == 'h' || data[0] == 'H')
		{
			_transite_state(state, _STATE_FIELD_NAME_HOST);
			internal->fn_state = _FIELD_NAME_HOST;
			return data + 1;
		}
		else if(data[0] == 'c' || data[1] == 'C')
		{
			internal->fn_state = _FIELD_NAME_CON_OR_CL;
			internal->sub_state = 0;
		}
		else if(data[0] == '\r')
		{
			_transite_state(state, _STATE_BODY_BEGIN);
			return data;
		}
		else 
		{
			_transite_state(state, _STATE_FIELD_NOT_INST);
			return data + 1;
		}
	}

	if(internal->fn_state == _FIELD_NAME_CON_OR_CL)
	{
		for(;data < end && internal->sub_state < 3; data++)
		{
			static const char common[] = "con";
			char ch = data[0];
			if(ch >= 'A' && ch <= 'Z')
				ch |= 0x20;

			if(common[internal->sub_state] == ch)
				internal->sub_state ++;
		}

		if(data == end) return end;
		switch(data[0])
		{
			case 'n':
				_transite_state(state, _STATE_FIELD_NAME_CONNECT);
				return data + 1;
			case 't':
				_transite_state(state, _STATE_FIELD_NAME_CONTENT_LEN);
				return data + 1;
			default:
				_transite_state(state, _STATE_FIELD_NOT_INST);
				return data + 1;
		}
	}

	return NULL;
}


#if 0
static inline int _parse_next_buf(parser_state_t* state, const char* data, size_t size)
{
	const char* end = data + size;
	_state_t* internal = (_state_t*)state->internal_state;
	
}
#endif

parser_state_t* parser_state_new()
{
	(void)_state_info;
	parser_state_t* ret = (parser_state_t*)calloc(sizeof(parser_state_t) + sizeof(_state_t), 1);

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the state");

	ret->empty = 1;

	return ret;
}
