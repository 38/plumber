/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <constants.h>

#if MODULE_TLS_ENABLED
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include <itc/module_types.h>
#include <itc/module.h>

#include <module/tls/bio.h>

#include <error.h>

#include <utils/log.h>
#include <utils/mempool/objpool.h>

#	if OPENSSL_VERSION_NUMBER < 0x10100000L
static BIO_METHOD _method;
#else
static BIO_METHOD* _method;
#	endif

static inline BIO_METHOD* _get_method(void);

BIO_METHOD* module_tls_bio_method(void)
{
	return _get_method();
}

BIO* module_tls_bio_new(module_tls_bio_context_t* ctx)
{
	BIO* ret = BIO_new(_get_method());

	if(NULL == ret)
	{
		LOG_ERROR("OpenSSL Error: %s", ERR_error_string(ERR_get_error(), NULL));
		return NULL;
	}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	ret->ptr = ctx;
	ret->init = 1;
#else
	BIO_set_data(ret, ctx);
	BIO_set_init(ret, 1);
#endif

	return ret;
}

/**
 * @brief initialize a pipe BIO
 * @param b the BIO buffer
 * @return OpenSSL status code
 **/
static int _create(BIO* b)
{
	if(NULL == b)
	{
		LOG_WARNING("Invalid arguments");
		return 0;
	}
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	b->init = 0;
	b->shutdown = 0;
	b->ptr = NULL;
#else
	BIO_set_init(b, 0);
	BIO_set_shutdown(b, 0);
	BIO_set_data(b, NULL);
#endif

	LOG_DEBUG("BIO object has been created");

	return 1;
}

/**
 * @brief properly cleanup a pipe BIO
 * @param b the BIO to clean
 * @return OpenSSL status code
 **/
static int _destroy(BIO* b)
{
	if(NULL == b)
	{
		LOG_WARNING("Invalid arguments");
		return 0;
	}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	b->init = 0;
#else
	BIO_set_init(b, 0);
#endif

	LOG_DEBUG("BIO object has been destroyed");

	return 1;
}

/**
 * @brief write bytes to the pipe BIO
 * @param b the BIO object
 * @param buf the data buffer
 * @param size the buffer length
 * @return the return code will be used by OpenSSL, it's different to Plumber convention <br/>
 *         &lt;0, write error, if retry flag is set, indicates it's a non-fatal error <br/>
 *         = 0, pipe closed, and not avaliable since then<br/>
 *         &gt;0, write success, return the number of bytes has written
 **/
static int _write(BIO* b, const char* buf, int size)
{
	if(NULL != b)  BIO_clear_retry_flags(b);

	if(NULL == b || NULL == buf)
	{
		LOG_WARNING("Invalid arguments");
		return 0;
	}

	if(size <= 0) return 0;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	module_tls_bio_context_t* ctx = (module_tls_bio_context_t*)b->ptr;
#else
	module_tls_bio_context_t* ctx = (module_tls_bio_context_t*)BIO_get_data(b);
#endif

	if(NULL == ctx)
	{
		LOG_WARNING("Invalid BIO Context");
		return 0;
	}

	size_t rc;

	if(NULL == ctx->buffer)
	{
		rc = itc_module_pipe_write(buf, (size_t)size, ctx->pipe);


		if(ERROR_CODE(size_t) == rc)
		{
			LOG_ERROR("Cannot write to the transportation layer pipe");
			return -1;
		}


		LOG_DEBUG("TLS BIO: write %zu bytes", rc);

		if(rc == 0) return -1;
	}
	else
	{
		rc = (size_t)size;
		if(rc > ctx->bufsize) rc = ctx->bufsize;

		memcpy(ctx->buffer, buf, rc);

		LOG_DEBUG("TLS BIO: copied %zu bytes to the DRA buffer", rc);

		ctx->buffer += rc;
		ctx->bufsize -= rc;

		BIO_clear_retry_flags(b);
		if(rc == 0)
		{
			LOG_DEBUG("The BIO output buffer is full, tell OpenSSL to try it later");
			BIO_set_retry_write(b);
			return -1;
		}
	}

	return (int)rc;
}

/**
 * @brief read from a pipe BIO
 * @param b the bio object
 * @param buf the read buffer
 * @param size the size of the buffer
 * @return the return code will be used by OpenSSL, it's different to Plumber convention <br/>
 *         &lt;0, read error, if retry flag is set, indicates it's a non-fatal error <br/>
 *         = 0, pipe closed, and not avaliable since then<br/>
 *         &gt;0, read success, return the number of bytes has read
 **/
static int _read(BIO* b, char* buf, int size)
{
	if(NULL != b)  BIO_clear_retry_flags(b);

	if(NULL == b || NULL == buf || size <= 0)
	{
		LOG_WARNING("Invalid arguments");
		return 0;
	}

	if(size <= 0) return 0;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	module_tls_bio_context_t* ctx = (module_tls_bio_context_t*)b->ptr;
#else
	module_tls_bio_context_t* ctx = (module_tls_bio_context_t*)BIO_get_data(b);
#endif

	if(NULL == ctx)
	{
		LOG_WARNING("Invalid BIO Context");
		return 0;
	}

	size_t rc = itc_module_pipe_read(buf, (size_t)size, ctx->pipe);

	if(ERROR_CODE(size_t) == rc)
	{
		LOG_ERROR("Cannot read from the transportation layer pipe");
		return -1;
	}

	LOG_DEBUG("TLS BIO: read %zu bytes", rc);

	if(rc == 0)
	{
		if(!itc_module_pipe_eof(ctx->pipe))
		{
			BIO_set_retry_read(b);
			return -1;
		}
		else return 0;
	}

	return (int)rc;
}

/**
 * @brief output a string to the BIO
 * @param b BIO object
 * @param buf string
 * @return seem as write
 **/
static int _puts(BIO* b, const char* buf)
{
	return NULL != buf ? _write(b, buf, (int)strlen(buf)) : 0;
}

/**
 * @brief the BIO control function, not actually used, but it's required
 * @param b BIO
 * @param cmd command
 * @param num the number
 * @param ptr the data pointer
 * @return openssl status
 **/
static long _ctrl(BIO *b, int cmd, long num, void *ptr)
{
	(void)b;
	(void)num;
	(void)ptr;

	return (cmd == BIO_CTRL_DUP || cmd == BIO_CTRL_FLUSH);
}

static inline BIO_METHOD* _get_method(void)
{
#	if OPENSSL_VERSION_NUMBER >= 0x10100000L
	if(NULL == _method)
	{
		_method = BIO_meth_new(MODULE_TLS_BIO_TYPE, "plumber TCP pipe wrapper");
		if(NULL == _method)
		{
			LOG_ERROR("Could not create method object for TLS BIO");
			return NULL;
		}

		BIO_meth_set_create(_method, _create);
		BIO_meth_set_destroy(_method, _destroy);
		BIO_meth_set_write(_method, _write);
		BIO_meth_set_read(_method, _read);
		BIO_meth_set_puts(_method, _puts);
		BIO_meth_set_ctrl(_method, _ctrl);
	}
	return _method;
#	else
	return &_method;
#	endif
}

void module_tls_bio_cleanup(void)
{
#	if OPENSSL_VERSION_NUMBER >= 0x10100000L
	if(NULL != _method)
		BIO_meth_free(_method);
#	endif
}

#	if OPENSSL_VERSION_NUMBER < 0x10100000L
/**
 * @brief the BIO method used for the TCP pipe BIO
 **/
static BIO_METHOD _method = {
	.type = MODULE_TLS_BIO_TYPE,
	.name = "plumber TCP pipe wrapper",
	.create = _create,
	.destroy = _destroy,
	.bwrite  = _write,
	.bread = _read,
	.bputs = _puts,
	.ctrl  = _ctrl,
	.bgets = NULL,
	.callback_ctrl = NULL
};
#	endif
#endif
