/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <pstd.h>

#include <request.h>
#include <connection.h>
#include <http.h>

#define _PAGESIZE 4096

/**
 * @brief The name of the HTTP verb
 **/
const char* _method_verb[] = {
	[REQUEST_METHOD_GET]  = "GET ",
	[REQUEST_METHOD_PUT]  = "PUT ",
	[REQUEST_METHOD_POST] = "POST ",
	[REQUEST_METHOD_HEAD] = "HEAD ",
	[REQUEST_METHOD_DELETE] = "DELETE "
};

/**
 * @brief The length of the HTTP verb
 **/
const size_t _method_verb_size[] = {
	[REQUEST_METHOD_GET]  = 4,
	[REQUEST_METHOD_PUT]  = 4,
	[REQUEST_METHOD_POST] = 5,
	[REQUEST_METHOD_HEAD] = 5,
	[REQUEST_METHOD_DELETE] = 7
};

/**
 * @brief The actual data structure of a HTTP request
 **/
struct _request_t {
	const char*       domain;           /*!< The begining of the domain name */
	const char*       host;             /*!< The begining of the host name (Which includes port and domain) */
	const char*       port_str;         /*!< The string reprentation of the port number */
	uint16_t          port;             /*!< The port */
	uint8_t           domain_len;       /*!< The length of the domain */
	uint8_t           host_len;         /*!< The length of the host */
	uint8_t           port_str_len:4;   /*!< The length of the port string */
	uint32_t          committed:1;      /*!< Indicates if this object has been commited */
	uint32_t          pooled_list:1;    /*!< Indicates the pool list is allocated in the page */
	request_method_t  method;           /*!< The method to use */
	char**            req_pages;        /*!< The pages that contains the request data */
	uint32_t          req_page_count;   /*!< The numer of request pages */
	uint32_t          req_page_offset;  /*!< The offset of the last page we used */
	uint32_t          req_page_capcity; /*!< The capacity of the page list */
	uint32_t          timeout;          /*!< The timeout for this request */
	/* TODO: add cookie, etc */
};

/**
 * @brief The data structure used for a request stream
 **/
typedef struct {
	const request_t*   req;                  /*!< The request data for this stream */
	int                sock;                 /*!< The socket we are using */
	uint32_t           cur_request_page;     /*!< The current request page */
	uint32_t           cur_request_page_ofs; /*!< The current request page offset */
	uint32_t           error:1;              /*!< Indicates if we are encounter some socket error */
	http_response_t    response;             /*!< The response state object */
} _stream_t;

static inline int _free_request_pages(request_t* req)
{
	if(req->req_pages == NULL) return 0;

	uint32_t i;
	int rc = 0;
	for(i = 0; i < req->req_page_count; i ++)
	    if(req->req_pages[i] != NULL && ERROR_CODE(int) == pstd_mempool_page_dealloc(req->req_pages[i]))
	        rc = ERROR_CODE(int);

	if(req->pooled_list && ERROR_CODE(int) == pstd_mempool_free(req->req_pages))
	    rc = ERROR_CODE(int);

	if(req->pooled_list == 0)
	    free(req->req_pages);

	return rc;
}

/**
 * @brief Ensure the request buffer have sufficient memory
 * @param sz The size of the data to write
 * @return The actual size that can be written
 **/
static inline size_t _ensure_request_pages(request_t* req, size_t sz)
{
	if(req->req_page_offset == _PAGESIZE)
	{
		if(req->req_page_capcity == req->req_page_count)
		{
			uint32_t new_cap = req->req_page_capcity * 2;
			char** prev_list = req->pooled_list ? NULL : req->req_pages;

			char** new_list = (char**)realloc(prev_list, sizeof(char*) * new_cap);

			if(NULL == new_list) ERROR_RETURN_LOG_ERRNO(size_t, "Cannot resize the page buffer");

			if(NULL == prev_list)
			    memcpy(new_list, req->req_pages, sizeof(char*) * req->req_page_capcity);

			memset(new_list + req->req_page_capcity, 0, sizeof(char*) * req->req_page_capcity);

			req->req_pages = new_list;
			req->req_page_capcity = new_cap;
		}

		if(NULL == (req->req_pages[req->req_page_count] = pstd_mempool_page_alloc()))
		    ERROR_RETURN_LOG(size_t, "Cannot allocate new page");

		req->req_page_count ++;
		req->req_page_offset = 0;
	}

	if(sz > _PAGESIZE - req->req_page_offset)
	    sz = _PAGESIZE - req->req_page_offset;

	return sz;
}

static inline int _request_buffer_write(request_t* req, const void* data, size_t size)
{
	for(;size > 0;)
	{
		size_t bytes_to_write = _ensure_request_pages(req, size);
		if(ERROR_CODE(size_t) == bytes_to_write)
		    ERROR_RETURN_LOG(int, "Cannot ensure the request buffer has enough memory");

		memcpy(req->req_pages[req->req_page_count - 1] + req->req_page_offset, data, bytes_to_write);

		size -= bytes_to_write;
		req->req_page_offset += (uint32_t)bytes_to_write;
	}

	return 0;
}

static inline int _populate_request_buffer(request_t* req, const char* base, const char* path, const char* query, uint64_t beg, uint64_t end, const void* data, size_t data_len)
{
	enum {
		_VERB,
		_URL_BASE,
		_URL_REL,
		_QUERY_START,
		_QUERY_BODY,
		_VERSION,
		_HOST_KEY,
		_HOST_VALUE,
		_CL_LINE,
		_RANGE_LINE,
		_MISC_FIELDS,
		_END_OF_HEADER,
		_BODY,
		_N_PARTS
	};
	const char* req_data[_N_PARTS] = {};
	size_t req_size[_N_PARTS] = {};
	char cl_buf[128];
	char rg_buf[128];
	size_t base_len;

#define _SET_CONST(what, str) req_data[what] = str, req_size[what] = sizeof(str) - 1
#define _SET_VAR(what, str, len) req_data[what] = str, req_size[what] = len
	_SET_VAR  (_VERB, _method_verb[req->method], _method_verb_size[req->method]);
	_SET_VAR  (_URL_BASE, base, base_len = strlen(base));
	if(base_len > 0 && base[base_len - 1] == '/' && path[0] == '/')
	    path ++;
	_SET_VAR  (_URL_REL, path, strlen(path));
	if(query[0] != 0)
	{
		_SET_CONST(_QUERY_START, "?");
		_SET_VAR  (_QUERY_BODY, query, strlen(query));
	}
	_SET_CONST(_VERSION, " HTTP/1.1\r\n");
	_SET_CONST(_HOST_KEY, "Host: ");
	_SET_VAR  (_HOST_VALUE, req->domain, strlen(req->domain));
	if(NULL != data)
	{
		int sz = snprintf(cl_buf, sizeof(cl_buf), "\r\nContent-Length: %zu\r\n", data_len);
		_SET_VAR(_CL_LINE, cl_buf, (size_t)sz);
	}
	else _SET_CONST(_CL_LINE, "\r\n");
	if(beg != (uint64_t)-1 || end != (uint64_t)-1)
	{
		int sz = 0;
		if(beg == (uint64_t)-1)
		    sz = snprintf(rg_buf, sizeof(rg_buf), "Range: bytes=-%"PRIu64"\r\n", end - 1);
		else if(end == (uint64_t) - 1)
		    sz = snprintf(rg_buf, sizeof(rg_buf), "Range: bytes=%"PRIu64"-\r\n", beg);
		else
		    sz = snprintf(rg_buf, sizeof(rg_buf), "Range: bytes=%"PRIu64"-%"PRIu64"\r\n", beg, end - 1);
		_SET_VAR(_RANGE_LINE, rg_buf, (size_t)sz);
	}
	_SET_CONST(_MISC_FIELDS, "User-Agent: Plumber(network.http.proxy)/0.1\r\nConnection: keep-alive\r\n");
	_SET_CONST(_END_OF_HEADER, "\r\n");
	if(data != NULL) _SET_VAR(_BODY, data, data_len);

	uint32_t i;

	for(i = 0; i < sizeof(req_data) /sizeof(req_data[0]); i ++)
	    if(req_data[i] != NULL && ERROR_CODE(int) == _request_buffer_write(req, req_data[i], req_size[i]))
	        ERROR_RETURN_LOG(int, "Cannot write data to the buffer");

	return 0;
}

request_t* request_new(const request_param_t* param, uint32_t timeout)
{
	if(NULL == param->host || param->base_dir == NULL || param->relative_path == NULL)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	request_t* ret = pstd_mempool_alloc(sizeof(request_t));

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new request object");

	ret->committed = 0;
	ret->domain = param->host;
	ret->req_pages = NULL;
	ret->method = param->method;

	ret->host = ret->domain;
	for(ret->domain_len = 0;; ret->domain_len ++)
	{
		char ch = ret->domain[ret->domain_len];
		if(ch == ':' || ch == '/' || ch == 0)
		    break;

		if(ret->domain_len == 0xff)
		    ERROR_LOG_GOTO(ERR, "The domain name is too long");
	}

	ret->port = 80;
	ret->port_str = NULL;

	if(ret->domain[ret->domain_len] == ':')
	{
		uint32_t port_num = 0;
		int i;

		ret->port_str = ret->domain + ret->domain_len + 1;

		for(i = ret->domain_len + 1; ret->domain[i] >= '0' && ret->domain[i] <= '9' && port_num < 0x10000; i ++)
		    port_num = port_num * 10 + (uint32_t)(ret->domain[i] - '0');

		if(port_num >= 0x10000)
		    ERROR_LOG_GOTO(ERR, "Invalid port number");

		ret->port = (uint16_t)port_num;
		ret->port_str_len = 0xfu & (uint8_t)(i - ret->domain_len - 1);
		ret->host_len = 0xffu & ((uint8_t)(ret->domain_len + ret->port_str_len) + 1u);
	}
	else
	    ret->host_len = ret->domain_len;

	ret->req_page_capcity = 4;
	ret->req_page_count = 0;
	ret->req_page_offset = _PAGESIZE;
	ret->pooled_list = 1;
	if(NULL == (ret->req_pages = (char**)pstd_mempool_alloc((uint32_t)(sizeof(char*) * ret->req_page_capcity))))
	    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the req page list");

	memset(ret->req_pages, 0, sizeof(char*) * ret->req_page_capcity);

	if(ERROR_CODE(int) == _populate_request_buffer(ret,
	                                               param->base_dir, param->relative_path, param->query_param,
	                                               param->range_begin, param->range_end,
	                                               param->content, param->content_len))
	    ERROR_LOG_GOTO(ERR, "Cannot populate the request buffer");

	ret->timeout = timeout;

	return ret;
ERR:
	_free_request_pages(ret);
	pstd_mempool_free(ret);
	return NULL;
}

static inline int _request_free(request_t* req)
{
	int rc = _free_request_pages(req);

	if(ERROR_CODE(int) == pstd_mempool_free(req))
	    rc = ERROR_CODE(int);

	return rc;
}


int request_free(request_t* req)
{
	if(NULL == req)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(req->committed)
	    ERROR_RETURN_LOG(int, "The request has been committed to RLS");

	return _request_free(req);
}

static int _rls_free(void* obj)
{
	request_t* req = (request_t*)obj;

	return _request_free(req);
}

static inline int _connect(_stream_t* stream)
{
	const request_t* req = stream->req;

	int conn_rc = connection_pool_checkout(req->domain, req->domain_len, req->port, &stream->sock);

	if(ERROR_CODE(int) == conn_rc)
	    ERROR_RETURN_LOG(int, "Cannot checkout the socket to the server from connection pool");

	if(conn_rc == 1)
	{
		char c;
		LOG_DEBUG("The connection pool returns a socket, try to validate the socket is in good state");
		ssize_t sz = recv(stream->sock, &c, 1, MSG_PEEK);

		if((sz < 0 && errno != EWOULDBLOCK && errno != EAGAIN) || sz == 0)
		{
			if(sz < 0)
			    LOG_DEBUG_ERRNO("The socket fd returns an unexpected FD, closing it and establish a new one");
			else
			    LOG_DEBUG("The socket is in half-closed state, get rid of that one");
			if(close(stream->sock) < 0)
			    LOG_WARNING_ERRNO("Cannot close the FD");
			conn_rc = 0;
		}
	}

	if(conn_rc == 0)
	{
		LOG_DEBUG("The connection pool doesn't have any connection can be used, try to open another one");

		char domain_buf[256];
		char port_buf[16];

		memcpy(domain_buf, req->domain, req->domain_len);
		domain_buf[req->domain_len] = 0;


		if(req->port_str == NULL)
		    memcpy(port_buf, "80", 3);
		else
		{
			memcpy(port_buf, req->port_str, req->port_str_len);
			port_buf[req->port_str_len] = 0;
		}

		struct addrinfo hints = {
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_flags = 0,
			.ai_protocol = 0
		}, *result, *ptr;

		int rc = getaddrinfo(domain_buf, port_buf, &hints, &result);

		if(rc != 0)
		    ERROR_RETURN_LOG(int, "Cannot resolve the domain name: %s", gai_strerror(rc));

		for(ptr = result; ptr != NULL; ptr = ptr->ai_next)
		{
			if((stream->sock =socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0)
			    LOG_TRACE_ERRNO("Cannot connect to the address %s", req->domain);

			if(connect(stream->sock, ptr->ai_addr, ptr->ai_addrlen) >= 0)
			{
				LOG_TRACE("The connection has been successfully established to %s", req->domain);
				int flags = fcntl(stream->sock, F_GETFL, 0);
				if(flags == -1)
				    ERROR_LOG_ERRNO_GOTO(CONN_FAIL, "Cannot get the flags for the socket FD");

				if(fcntl(stream->sock, F_SETFL, flags | O_NONBLOCK) < 0)
				    ERROR_LOG_ERRNO_GOTO(CONN_FAIL, "Cannot set the socket FD to nonblocking mode");

				conn_rc = 1;
				break;
			}
CONN_FAIL:
			if(stream->sock >= 0 && close(stream->sock) < 0)
			    LOG_WARNING("Cannot close the socket fd %d", stream->sock);

		}

		if(NULL != result)
		    freeaddrinfo(result);

		if(conn_rc == 0)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot connect to the server");
	}

	return 0;
}

static int _rls_close(void* obj)
{
	int  rc = 0, needs_close = 0;
	_stream_t* stream = (_stream_t*)obj;

	if(stream->sock >= 0)
	{

		/* First of all, we need to shut down all the socket that is wrong */
		if(stream->error) needs_close = 1;

		/* Then we need to dealing with the socket that still have undergoing data transferring */
		if(!http_response_complete(&stream->response))
		{

			LOG_DEBUG("The stream has to be closed because client has shutted down");

			/* Try to read at most once to see if we can see the end of message, if we can see the end of message, we saved this connection */
			char buf[4096];

			ssize_t sz = read(stream->sock, buf, sizeof(buf));

			if(sz < 0)
			{
				if(errno == EPIPE)
				    LOG_TRACE_ERRNO("Could not read more data, because the socket is half-closed");
				else if(errno == EWOULDBLOCK || errno == EAGAIN)
				    LOG_TRACE_ERRNO("We are waiting for the connection gets ready, stop");
				else
				    LOG_WARNING_ERRNO("The remote server peer socket is error");

				needs_close = 1;
			}

			if(sz > 0 && 1 == http_response_parse(&stream->response, buf, (size_t)sz) && http_response_complete(&stream->response))
			    LOG_DEBUG("We finally figured out where the message ends, checkin the socket instread of close");
			else
			    needs_close = 1;
		}

		if(needs_close && close(stream->sock) < 0)
		{
			rc = ERROR_CODE(int);
			LOG_ERROR_ERRNO("Cannot close the error socket");
		}

		if(!needs_close && ERROR_CODE(int) == connection_pool_checkin(stream->req->domain, stream->req->domain_len, stream->req->port, stream->sock))
		{
			rc = ERROR_CODE(int);
			LOG_ERROR("Cannot checkin the connection to the connection pool");
			close(stream->sock);
		}
	}

	if(ERROR_CODE(int) == pstd_mempool_free(stream))
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot dispose the closed stream");
	}

	return rc;
}

static void* _rls_open(const void* obj)
{
	const request_t* req = (const request_t*)obj;
	_stream_t* stream = (_stream_t*)pstd_mempool_alloc(sizeof(_stream_t));

	if(NULL == stream)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new stream");

	stream->sock = -1;
	stream->req = req;
	stream->cur_request_page = 0;
	stream->cur_request_page_ofs = 0;
	memset(&stream->response, 0, sizeof(stream->response));

	if(ERROR_CODE(int) == _connect(stream))
	    ERROR_LOG_GOTO(ERR, "Cannot connect to the server");

	stream->error = 0;

	return stream;
ERR:

	if(stream->sock >= 0 && connection_pool_checkin(req->domain, req->domain_len, req->port, stream->sock) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot checkin the connected FD");
		close(stream->sock);
	}

	pstd_mempool_free(stream);
	return NULL;
}

static inline int _end_of_request(_stream_t* stream)
{
	return stream->cur_request_page + 1> stream->req->req_page_count ||
	       (stream->cur_request_page == stream->req->req_page_count - 1 &&
	        stream->cur_request_page_ofs >= stream->req->req_page_offset);
}

static size_t _rls_read(void* __restrict obj, void* __restrict buf, size_t count)
{
	_stream_t* stream = (_stream_t*)obj;
	const request_t* req = stream->req;

	while(!_end_of_request(stream))
	{
		size_t bytes_to_write = count;
		size_t current_page_size = req->req_page_count - 1 == stream->cur_request_page ? req->req_page_offset : _PAGESIZE;

		if(current_page_size - stream->cur_request_page_ofs < bytes_to_write)
		    bytes_to_write = current_page_size - stream->cur_request_page_ofs;

		ssize_t bytes_written = write(stream->sock,  req->req_pages[stream->cur_request_page] + stream->cur_request_page_ofs, bytes_to_write);

		if(bytes_written == -1)
		{
			if(errno == EWOULDBLOCK || errno == EAGAIN)
			    return 0;
			/* TODO: output the 503 message */
			stream->error = 1;
			LOG_TRACE_ERRNO("The socket cannot be written");
			return ERROR_CODE(size_t);
		}

		stream->cur_request_page_ofs += (uint32_t)bytes_written;

		if(stream->cur_request_page_ofs == _PAGESIZE)
		    stream->cur_request_page_ofs = 0, stream->cur_request_page ++;
	}

	ssize_t bytes_read = read(stream->sock, buf, count);

	if(bytes_read == -1)
	{
		if(errno == EWOULDBLOCK || errno == EAGAIN)
		    return 0;
		stream->error = 1;
		LOG_TRACE_ERRNO("The socket cannot be read");
		return ERROR_CODE(size_t);
	}
	else if(bytes_read == 0)
	{
		LOG_TRACE_ERRNO("The socket has ben closed");
		stream->error = 1;
		return 0;
	}

	int rc = http_response_parse(&stream->response, buf, (size_t)bytes_read);
	if(rc == ERROR_CODE(int))
	    ERROR_RETURN_LOG(size_t, "Cannot parse the response");
	else if(rc == 0)
	{
		LOG_TRACE("The response is not valid anymore, we need to purge the connection");
		stream->error =1;
		return ERROR_CODE(size_t);
	}

	return (size_t)bytes_read;
}

static inline int _rls_eos(const void* obj)
{
	const _stream_t* stream = (const _stream_t*)obj;

	return stream->error || http_response_complete(&stream->response);
}

static int _rls_event(void* obj, scope_ready_event_t* buf)
{
	_stream_t* stream = (_stream_t*)obj;

	buf->fd = stream->sock;
	buf->timeout = (int32_t)stream->req->timeout;

	buf->read = 0;
	buf->write = 0;

	if(_end_of_request(stream))
	    buf->read = 1;
	else
	    buf->write = 1;

	return 1;
}

scope_token_t request_commit(request_t* request)
{
	if(NULL == request || request->committed)
	    ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	scope_entity_t ent = {
		.data = request,
		.free_func = _rls_free,
		.copy_func = NULL,
		.open_func = _rls_open,
		.close_func = _rls_close,
		.eos_func = _rls_eos,
		.read_func = _rls_read,
		.event_func = _rls_event
	};

	scope_token_t ret = pstd_scope_add(&ent);

	if(ERROR_CODE(scope_token_t) == ret)
	    ERROR_RETURN_LOG(scope_token_t, "Cannot add the entity to the scope");

	return ret;
}
