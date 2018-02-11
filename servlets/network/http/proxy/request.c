/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <pstd.h>

#include <request.h>
#include <connection.h>

/**
 * @brief The actual data structure of a HTTP request
 **/
struct _request_t {
	uint32_t          commited:1; /*!< Indicates if this object has been commited */
	char*             url;        /*!< The URL to request */
	char*             data;       /*!< The payload data */
	const char*       domain;     /*!< The begining of the domain name */
	size_t            domain_len; /*!< The length of the domain */
	uint16_t          port;       /*!< The port */
	request_method_t  method;     /*!< The method to use */
	/* TODO: add cookie, etc */
};

/**
 * @brief The data structure used for a request stream
 **/
typedef struct {
	const request_t*   req;         /*!< The request data for this stream */
	int                sock;        /*!< The socket we are using */
	uint32_t           error:1;     /*!< Indicates if we are encounter some socket error */
	uint32_t           requested:1; /*!< Indicates if we have sent the request */
} _stream_t;

request_t* request_new(request_method_t method, const char* url, const char* data)
{
	if(NULL == url)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(memchr(url, 0, 7) != NULL || memcmp(url, "http://", 7) != 0)
		ERROR_PTR_RETURN_LOG("Invalid URL");

	request_t* ret = pstd_mempool_alloc(sizeof(request_t));

	if(NULL == ret) 
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new request object");

	ret->commited = 0;
	ret->data = NULL;
	if(NULL == (ret->url = strdup(url)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the URL string");

	if(NULL != data && NULL == (ret->data = strdup(data)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the data string");

	ret->method = method;

	ret->domain = ret->url + 7;
	for(ret->domain_len = 0;; ret->domain_len ++)
	{
		char ch = ret->domain[ret->domain_len];
		if(ch == ':' || ch == '/' || ch == 0)
			break;
	}

	ret->port = 80;

	if(ret->domain[ret->domain_len] == ':')
	{
		ret->port = 0;
		size_t i;
		for(i = ret->domain_len + 1; ret->domain[i] >= '0' && ret->domain[i] <= '9'; i ++)
			ret->port = (uint16_t)(ret->port * 10 + (ret->domain[i] - '0'));
	}

	return ret;
ERR:
	if(NULL != ret->data) free(ret->data);
	if(NULL != ret->url) free(ret->url);
	pstd_mempool_free(ret);
	return NULL;
}

static inline int _request_free(request_t* req)
{
	if(NULL != req->data) free(req->data);
	if(NULL != req->url)  free(req->url);
	return pstd_mempool_free(req);
}


int request_free(request_t* req)
{
	if(NULL == req) 
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(req->commited)
		ERROR_RETURN_LOG(int, "The request has been committed to RLS");

	return _request_free(req);
}

inline /* TODO: remove this */
static int _rls_free(void* obj)
{
	request_t* req = (request_t*)obj;

	return _request_free(req);
}

inline /* TODO: remove this */
static void* _rls_open(const void* obj)
{
	const request_t* req = (const request_t*)obj;
	_stream_t* stream = (_stream_t*)pstd_mempool_alloc(sizeof(_stream_t));

	if(NULL == stream)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new stream");

	stream->sock = -1;
	stream->req = req;

	int conn_rc = connection_pool_checkout(req->domain, req->domain_len, req->port, &stream->sock);

	if(ERROR_CODE(int) == conn_rc)
		ERROR_LOG_GOTO(ERR, "Cannot checkout the socket FD from the connection pool");

	if(conn_rc == 1)
	{
		char c;
		LOG_DEBUG("The connection pool returns a socket, try to validate the socket is in good state");
		if(recv(stream->sock, &c, 1, MSG_PEEK) < 0)
		{
			if(errno != EWOULDBLOCK && errno != EAGAIN)
			{
				LOG_DEBUG("The socket fd returns an unexpected FD, closing it and establish a new one");
				close(stream->sock);
				conn_rc = 0;
			}
		}
	}

	if(conn_rc == 0)
	{
		LOG_DEBUG("The connection pool doesn't have any connection can be used, try to open another one");

		char domain_buf[256];
		size_t size = req->domain_len > sizeof(domain_buf) - 1 ? sizeof(domain_buf) - 1 : req->domain_len;

		memcpy(domain_buf, req->domain, size);
		domain_buf[size] = 0;

		char port_buf[32];
		size_t port_len = 0;
		for(;;)
		{
			char ch = req->domain[req->domain_len + port_len];
			if(ch < '0' || ch > '9') break;
			port_buf[port_len++] = ch;
		}

		if(port_len == 0) 
			memcpy(port_buf, "80", 3);
		else
			port_buf[port_len] = 0;

		struct addrinfo hints = {
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_flags = 0,
			.ai_protocol = 0
		}, *result, *ptr;

		int rc = getaddrinfo(domain_buf, port_buf, &hints, &result);

		if(rc != 0)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resolve the domain name for URL: %s (%s)",
					                  req->url, gai_strerror(rc));

		for(ptr = result; ptr != NULL; ptr = ptr->ai_next)
		{
			if((stream->sock =socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0)
				LOG_TRACE_ERRNO("Cannot connect to the address %s", req->url);

			if(connect(stream->sock, ptr->ai_addr, ptr->ai_addrlen) < 0)
			{
				LOG_TRACE("The connection has been successfully established to %s", req->url);
				conn_rc = 1;
				break;
			}
			else if(stream->sock >= 0 && close(stream->sock) < 0)
				LOG_WARNING("Cannot close the socket fd %d", stream->sock);
		}

		if(conn_rc == 0)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot establish connection to the server");
	}

	stream->error = 0;
	stream->requested = 0;
	return stream;

ERR:
	/* TODO: do we need to reuse the socket, but currently this code will never trigger in fact */
	if(stream->sock > 0) close(stream->sock);

	pstd_mempool_free(stream);

	return NULL;
}
