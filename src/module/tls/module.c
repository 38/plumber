/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <constants.h>

#if MODULE_TLS_ENABLED
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/conf.h>
#include <openssl/engine.h>

#include <error.h>
#include <utils/log.h>
#include <utils/mempool/objpool.h>
#include <utils/thread.h>

#include <itc/module_types.h>
#include <itc/module.h>
#include <itc/modtab.h>

#include <module/tls/module.h>
#include <module/tls/bio.h>
#include <module/tls/api.h>
#include <module/tls/dra.h>

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
/**
 * @brief The supported ALPN protocol names, it should be the NULL terminated strings
 **/
static const char* _supported_alpn_protos[] = {
	"h2",       /*!< The HTTP/2 protocol */
	"http/1.1", /*!< The HTTP/1.1 Protocol */
	"spdy/1",   /*!< The SPDY/1 Protocol */
	"spdy/2",   /*!< The SPDY/2 Protocol */
	"spdy/3"    /*!< The SPDY/3 Protocol */
};

typedef struct {
	uint8_t  begin[0];/*!< Where the actual data section begins */
	uint8_t  length;  /*!< The length of the protocl */
	uint8_t  name[0]; /*!< The name of the protocol */
} __attribute__((packed)) _alpn_protocol_t;
STATIC_ASSERTION_FOLLOWS(_alpn_protocol_t, begin, length);
STATIC_ASSERTION_SIZE(_alpn_protocol_t, begin, 0);
STATIC_ASSERTION_SIZE(_alpn_protocol_t, length, 1);
STATIC_ASSERTION_SIZE(_alpn_protocol_t, name, 0);
STATIC_ASSERTION_LAST(_alpn_protocol_t, name);
STATIC_ASSERTION_FOLLOWS(_alpn_protocol_t, length, name);
#endif

/**
 * @brief the type describes the direction of the handle
 **/
typedef enum {
	_HANDLE_TYPE_IN,   /*!< a input handle */
	_HANDLE_TYPE_OUT   /*!< a output handle */
} _handle_type_t;

/**
 * @brief the state of the TLS state
 **/
typedef enum {
	_TLS_STATE_CONNECTING,   /*!< the TLS library is performing connect operation */
	_TLS_STATE_CONNECTED,    /*!< the TLS tunnel has been established */
	_TLS_STATE_DISABLED      /*!< the TLS encryption is disabled */
} _tls_state_t;

/**
 * @brief the previous definition for the module context
 **/
typedef struct _module_context_t _module_context_t;

/**
 * @brief the TLS context
 **/
typedef struct _module_tls_module_conn_data_t {
	module_tls_bio_context_t        in_bio_ctx;           /*!< the BIO context used by this TLS context */
	module_tls_bio_context_t        out_bio_ctx;          /*!< the BIO context used by this TLS context */
	_module_context_t*              module_context;       /*!< the module context used by this connection context */
	_tls_state_t                    state;                /*!< the state of the TLS connection */
	SSL*                            ssl;                  /*!< the ssl context data */
	uint32_t                        user_state_to_push:1; /*!< indicate this is the user-space state to push */
	uint32_t                        pushed:1;             /*!< set when the state is already pushed */
	uint32_t                        dra_counter;          /*!< The shared memory used by the DRA synchornization */
	uint32_t                        refcnt;               /*!< The reference counter indicates when to dispose the context */
	void*                           user_state;           /*!< the user space state */
	itc_module_state_dispose_func_t dispose_user_state;   /*!< the callback function to dispose user defined state */
	char*                           unread_data;          /*!< unread data */
	uint32_t                        unread_data_size;     /*!< the size of unread data */
	uint32_t                        unread_data_start;    /*!< the data pointer for really unread data */
} _tls_context_t;

/**
 * @brief the TLS pipe handle
 **/
typedef struct {
	_tls_context_t*             tls;            /*!< the TLS related data */
	_handle_type_t              type;           /*!< the handle type */
	uint32_t                    last_read_size; /*!< the size of the last read call, only used in read pipes */
	uint32_t                    no_more_input;  /*!< if the handle is *definitely* no more input */
	itc_module_pipe_t*          t_pipe;         /*!< the transportation layer pipe */
} _handle_t;

/**
 * @brief the module context
 **/
struct _module_context_t{
	SSL_CTX*                       ssl_context;   /*!< the SSL Context */
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
	union {
		_alpn_protocol_t*          alpn_protos;   /*!< The list of ALPN protocols */
		uint8_t*                   alpn_data;     /*!< The ALPN protocol data */
	};
#endif
	itc_module_type_t              transport_mod; /*!< the transportation layer module type */
	uint32_t                       async_write;   /*!< indicates if we want to enable the asnyc write in the transportation layer */
	mempool_objpool_t*             tls_pool;      /*!< the SSL context pool */
};

/**
 * @brief the mutex used by the OpenSSL
 **/
static pthread_mutex_t* _ssl_mutex = NULL;

/**
 * @brief the number of the mutex use by the OpenSSL
 **/
static uint32_t _ssl_mutex_count;

/**
 * @brief the module instance count
 **/
static uint32_t _module_instance_count = 0;

/**
 * @brief if the string a is prefix-ed by string b
 * @param a the string a
 * @param b the string b
 * @return if the string matches
 **/
static inline int _match(const char* a, const char* b)
{
	for(;*a && *b; a ++, b ++)
	    if(*a != *b) break;

	return *b == 0;
}

/**
 * @brief the lock function used by OpenSSL
 * @param mode the lock operation selector
 * @param n the lock index
 * @param file the source code file name
 * @param line the line number
 * @return nothing
 **/
static void _ssl_lock(int mode, int n, const char * file, int line)
{
	(void)file;
	(void)line;
	if(mode & CRYPTO_LOCK)
	    pthread_mutex_lock(_ssl_mutex + n);
	else
	    pthread_mutex_unlock(_ssl_mutex + n);
}

/**
 * @brief get current thread id
 * @return thread id
 **/
static unsigned long _ssl_tid(void)
{
	  return (unsigned long) pthread_self();
}

/**
 * @brief initialize the threading callbacks for the OpenSSL
 * @return status code
 **/
static inline int _thread_init(void)
{
	uint32_t i;
	_ssl_mutex_count = (uint32_t)CRYPTO_num_locks();

	if(_ssl_mutex_count == 0) return 0;

	if(NULL == (_ssl_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * _ssl_mutex_count)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the ssl mutex array");

	for(i = 0; i < _ssl_mutex_count; i ++)
	    if((errno = pthread_mutex_init(_ssl_mutex + i, NULL)) != 0)
	        ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the mutex");

	CRYPTO_set_id_callback(_ssl_tid);
	CRYPTO_set_locking_callback(_ssl_lock);
	return 0;

ERR:

	if(NULL != _ssl_mutex)
	{
		uint32_t j;
		for(j = 0; j < i; j ++)
		    pthread_mutex_destroy(_ssl_mutex + i);
		free(_ssl_mutex);
	}
	_ssl_mutex = NULL;
	_ssl_mutex_count = 0;
	return ERROR_CODE(int);
}

/**
 * @brief do the cleanup for the OpenSSL mutexes
 * @return status code
 **/
static inline int _thread_finalize(void)
{
	if(_ssl_mutex == NULL) return 0;
	uint32_t i;
	int rc = 0;

	for(i = 0; i < _ssl_mutex_count; i ++)
	    if((errno = pthread_mutex_destroy(_ssl_mutex + i)) != 0)
	        rc = ERROR_CODE(int);

	free(_ssl_mutex);
	_ssl_mutex = NULL;
	_ssl_mutex_count = 0;
	return rc;
}



/**
 * @brief the initialization function of the TLS module
 * @details the initialization argument should look like <br/>
 *          <code>insmod "tls_pipe cert=cert.pem key=key.pem module.tcp.port_433";</code>
 * @param ctx the context buffer
 * @param argc the argument count
 * @param argv the argument value
 * @return the status code
 **/
static int _init(void* __restrict ctx, uint32_t argc, char const* __restrict const* __restrict argv)
{

	if(NULL == ctx) ERROR_RETURN_LOG(int, "Invalid arguments");

	_module_context_t* context = (_module_context_t*)ctx;

	const char* cert_file = "cert.pem";
	const char* pkey_file = "pkey.pem";

	static const char cert_prefix[] = "cert=";
	static const char pkey_prefix[] = "key=";

	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		char const* param = argv[i];
		if(_match(param, cert_prefix))
		    cert_file = param + sizeof(cert_prefix) - 1;
		else if(_match(param, pkey_prefix))
		    pkey_file = param + sizeof(pkey_prefix) - 1;
		else break;
	}

	LOG_DEBUG("Initialize OpenSSL with Certification %s and Private Key %s", cert_file, pkey_file);

	if(argc - i != 1) ERROR_RETURN_LOG(int, "Invalid module init args, should be"
	                                        "tls_pipe [cert=<CERT>] [key=<PKEY>] <trans-module-path>");

	/* Find the transportation layer module */
	if(ERROR_CODE(itc_module_type_t) == (context->transport_mod = itc_modtab_get_module_type_from_path(argv[i])))
	    ERROR_RETURN_LOG(int, "Cannot get the transportation layer module instance %s", argv[i]);

	/* Check if the module has the event-accepting flag, the transportation layer module should not have the flag */
	itc_module_flags_t mf = itc_module_get_flags(context->transport_mod);
	if(ERROR_CODE(itc_module_flags_t) == mf) ERROR_RETURN_LOG(int, "Cannot get the module flags of the module instance %s", argv[i]);
	if(0 != (mf & ITC_MODULE_FLAGS_EVENT_LOOP)) ERROR_RETURN_LOG(int, "Cannot use an event-accepting module as transprotation module");

	/* Initialize the OpenSSL */
	if(_module_instance_count == 0)
	{
		SSL_library_init();
		OpenSSL_add_all_algorithms();
		SSL_load_error_strings();
		_thread_init();
		if(ERROR_CODE(int) == module_tls_dra_init())
		    ERROR_RETURN_LOG(int, "Cannot initialize the DRA callback wrapper");
	}

	_module_instance_count ++;

	/* Initialize the SSL context for the module instance */
	const SSL_METHOD *method;
	if(NULL == (method = SSLv23_server_method()))
	    ERROR_RETURN_LOG(int, "Cannot get the server method: %s", ERR_error_string(ERR_get_error(), NULL));

	if(NULL == (context->ssl_context = SSL_CTX_new(method)))
	    ERROR_RETURN_LOG(int, "Cannot initialize the server context: %s", ERR_error_string(ERR_get_error(), NULL));

	/* Load and verify the certificate-key pair */
	if(SSL_CTX_use_certificate_file(context->ssl_context, cert_file, SSL_FILETYPE_PEM) <= 0)
	    ERROR_RETURN_LOG(int, "Cannot load the certificate: %s", ERR_error_string(ERR_get_error(), NULL));

	if(SSL_CTX_use_PrivateKey_file(context->ssl_context, pkey_file, SSL_FILETYPE_PEM) <= 0)
	    ERROR_RETURN_LOG(int, "Cannot load the private key: %s", ERR_error_string(ERR_get_error(), NULL));

	if(!SSL_CTX_check_private_key(context->ssl_context))
	    ERROR_RETURN_LOG(int, "Certificate and Private key are not match: %s", ERR_error_string(ERR_get_error(), NULL));

	context->async_write = 1;

	if(NULL == (context->tls_pool = mempool_objpool_new(sizeof(_tls_context_t))))
	{
		SSL_CTX_free(context->ssl_context);
		ERROR_RETURN_LOG(int, "Cannot create memory pool for the TLS context");
	}


#if OPENSSL_VERSION_NUMBER >= 0x10002000L
	context->alpn_protos = NULL;
#endif

	LOG_TRACE("TLS context has been initialized!");

	return 0;
}

/**
 * @brief cleanup the module
 * @param ctx the module context
 * @return status code
 **/
static int _cleanup(void* __restrict ctx)
{
	int rc = 0;
	_module_context_t* context = (_module_context_t*)ctx;

	SSL_CTX_free(context->ssl_context);

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
	if(NULL !=  context->alpn_protos)
	    free(context->alpn_protos);
#endif

	if(ERROR_CODE(int) == mempool_objpool_free(context->tls_pool))
	{
		rc = ERROR_CODE(int);
		LOG_WARNING("Cannot dispose the object pool for the TLS connection context");
	}

	if(0 == --_module_instance_count)
	{
		LOG_DEBUG("Finalize the OpenSSL library");
		_thread_finalize();
		CRYPTO_set_locking_callback(NULL);
		CRYPTO_set_id_callback(NULL);
		ENGINE_cleanup();
		CONF_modules_unload(1);
		ERR_free_strings();
		EVP_cleanup();
		CRYPTO_cleanup_all_ex_data();
		void* ptr = SSL_COMP_get_compression_methods();
		if(NULL != ptr) sk_free(ptr);
		if(ERROR_CODE(int) == module_tls_dra_finalize())
		{
			LOG_WARNING("Cannot fianlize the static variables used by DRA callback object");
			rc = ERROR_CODE(int);
		}
	}

	/* cleanup for the dispatcher */
	ERR_remove_state(0);

	return rc;
}

/**
 * @brief allocate a new TLS connection context
 * @note this data also used as the TLS status
 * @param context the module context
 * @return the newly create context, NULL on error case
 **/
static inline _tls_context_t* _tls_context_new(_module_context_t* context)
{
	_tls_context_t* ret = mempool_objpool_alloc(context->tls_pool);

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new TLS context");

	ret->state = _TLS_STATE_CONNECTING;
	if(NULL == (ret->ssl = SSL_new(context->ssl_context)))
	    ERROR_PTR_RETURN_LOG("Cannot create new SSL context: %s", ERR_error_string(ERR_get_error(), NULL));

	ret->user_state = NULL;
	ret->dispose_user_state = NULL;
	ret->module_context = context;

	ret->unread_data = NULL;
	ret->unread_data_size = 0;
	ret->unread_data_start = 0;
	ret->pushed = 0;
	ret->dra_counter = 0;
	ret->refcnt = 1;

	/* Initialize the BIO which uses the pipe */
	BIO* rbio = NULL;
	BIO* wbio = NULL;

	ret->in_bio_ctx.pipe = NULL;
	ret->in_bio_ctx.buffer = NULL;
	ret->in_bio_ctx.bufsize = 0;
	rbio = module_tls_bio_new(&ret->in_bio_ctx);
	if(NULL == rbio) ERROR_LOG_GOTO(L_ERR, "Cannot create the BIO for the input pipe");

	ret->out_bio_ctx.pipe = NULL;
	ret->out_bio_ctx.buffer = NULL;
	ret->out_bio_ctx.bufsize = 0;
	wbio = module_tls_bio_new(&ret->out_bio_ctx);
	if(NULL == wbio) ERROR_LOG_GOTO(L_ERR, "Cannot create the BIO for the output pipe");

	SSL_set_bio(ret->ssl, rbio, wbio);

	return ret;
L_ERR:
	if(rbio != NULL) BIO_free(rbio);
	if(wbio != NULL) BIO_free(wbio);
	SSL_free(ret->ssl);
	mempool_objpool_dealloc(context->tls_pool, ret);
	return NULL;
}

/**
 * @brief dispose the user-space state pointer in a TLS context
 * @param tls the TLS context
 * @return status code
 **/

static inline int _user_space_state_dispose(_tls_context_t* tls)
{
	if(NULL != tls->user_state && (NULL == tls->dispose_user_state || tls->dispose_user_state(tls->user_state) == ERROR_CODE(int)))
	    LOG_WARNING("Cannot call the dispose function for the user-defined state, memory leak possible");

	tls->user_state = NULL;

	return 0;
}
/**
 * @brief dispose the TLS connection context
 * @param ctx the context to dispose
 * @return the status code
 **/
static inline int _tls_context_free(_tls_context_t* context)
{
	if(NULL != context->ssl) SSL_free(context->ssl);

	if(ERROR_CODE(int) == _user_space_state_dispose(context))
	    ERROR_RETURN_LOG(int, "Cannot dispose the user-space state");

	if(ERROR_CODE(int) == mempool_objpool_dealloc(context->module_context->tls_pool, context))
	    ERROR_RETURN_LOG(int, "Cannot dispose the used state");

	if(context->unread_data != NULL) free(context->unread_data);

	return 0;
}

/**
 * @brief Decref the TLS context
 * @param ctx The context
 * @return status code
 **/
static inline int _tls_context_decref(void* ctx)
{
	_tls_context_t* context = (_tls_context_t*)ctx;
	uint32_t old;

	do {
		old = context->refcnt;
	} while(!__sync_bool_compare_and_swap(&context->refcnt, old, old - 1));

	if(old == 1) return _tls_context_free(context);

	return 0;
}

/**
 * @brief Increase the reference counter of the TLS context
 * @param context The context
 * @return status code
 **/
static inline int _tls_context_incref(_tls_context_t* context)
{
	uint32_t old;
	do {
		old = context->refcnt;
	} while(!__sync_bool_compare_and_swap(&context->refcnt, old, old + 1));

	return 0;
}

/**
 * @brief the helper function to invoke the pipe_cntl API call
 * @param pipe the pipe handle
 * @param opcode the opcode
 * @return status code
 **/
static inline int _invoke_pipe_cntl(itc_module_pipe_t* pipe, uint32_t opcode, ...)
{
	va_list ap;
	va_start(ap, opcode);
	int rc = itc_module_pipe_cntl(pipe, opcode, ap);
	va_end(ap);
	return rc;
}

/**
 * @brief the callback function used to cleanup the openssl library for each thread
 * @param thread the thread data
 * @param data the function additional data
 * @return status code
 **/
static inline int _clean_openssl(void* thread, void* data)
{
	(void)thread;
	(void)data;
	ERR_remove_state(0);

	return 0;
}

/**
 * @brief clear the open ssl error
 * @return status code
 **/
static inline int _clear_ssl_error(void)
{
	static __thread int hook_installed = 0;
	if(hook_installed == 0)
	{
		if(thread_add_cleanup_hook(_clean_openssl, NULL) == ERROR_CODE(int))
		    LOG_WARNING("Cannot setup the cleanup hook");
		else
		    hook_installed = 1;
	}
	ERR_clear_error();

	return 0;
}

/**
 * @brief accept a TLS connection
 * @param ctx the module context
 * @param args the accept arguments
 * @param inbuf the input buffer
 * @param outbuf the output buffer
 * @return status code
 **/
static int _accept(void* __restrict ctx, const void* __restrict args, void* __restrict inbuf, void* __restrict outbuf)
{
	_module_context_t* context = (_module_context_t*)ctx;
	runtime_api_pipe_flags_t in_flags  = itc_module_get_handle_flags(inbuf);
	runtime_api_pipe_flags_t out_flags = itc_module_get_handle_flags(outbuf);

	/* It should be persist before we established the connection and return the handle to user space,
	 * Becuase we need to do connect before the actual user space program can use this */
	itc_module_pipe_param_t trans_param = {
		.input_flags = in_flags | RUNTIME_API_PIPE_PERSIST,
		.output_flags = out_flags | RUNTIME_API_PIPE_PERSIST,
		.args = args
	};

	_handle_t* in = (_handle_t*)inbuf;
	_handle_t* out = (_handle_t*)outbuf;

	if(context->async_write == 1) trans_param.output_flags |= RUNTIME_API_PIPE_ASYNC;

	itc_module_pipe_t *trans_in = NULL;
	itc_module_pipe_t *trans_out = NULL;
	_tls_context_t* tls_state = NULL;
	/* Indicates if the TLS state is owned by this module */
	uint32_t state_owned = 0;

	if(itc_module_pipe_accept(context->transport_mod, trans_param, &trans_in, &trans_out) == ERROR_CODE(int) ||
	   NULL == trans_in ||
	   NULL == trans_out)
	    ERROR_LOG_GOTO(L_ERR, "Cannot accept connection from transportation layer");

	if(ERROR_CODE(int) == _invoke_pipe_cntl(trans_in, RUNTIME_API_PIPE_CNTL_OPCODE_POP_STATE, &tls_state))
	    ERROR_LOG_GOTO(L_ERR, "Cannot pop the previous state from the pipe");

	if(NULL == tls_state)
	{
		LOG_DEBUG("The connection has no TLS state been pushed, allocate a new one");
		if(NULL == (tls_state = _tls_context_new(context)))
		    ERROR_LOG_GOTO(L_ERR, "Cannot create new TLS context");
		state_owned = 1;
	}

	/* We need to update the BIO transporation layer pointer */
	tls_state->in_bio_ctx.pipe = trans_in;
	tls_state->out_bio_ctx.pipe = trans_out;

	/* Popup the pipe to user-space code */
	tls_state->user_state_to_push = 0;

	if(_TLS_STATE_DISABLED == tls_state->state)
	    tls_state->state = _TLS_STATE_CONNECTING;

	in->type = _HANDLE_TYPE_IN;
	in->tls = tls_state;
	in->t_pipe = trans_in;
	in->last_read_size = 0;
	in->no_more_input = 0;

	out->type = _HANDLE_TYPE_OUT;
	out->tls = tls_state;
	out->t_pipe = trans_out;

	return 0;

L_ERR:
	if(NULL != trans_in)   itc_module_pipe_deallocate(trans_in);
	if(NULL != trans_out)  itc_module_pipe_deallocate(trans_out);
	if(NULL != tls_state && state_owned) _tls_context_free(tls_state);
	return ERROR_CODE(int);
}

/**
 * @brief ensure that the TLS tunnel has been established
 * @param handle the handle to ensure
 * @return &gt;0 - The TLS tunnel is current avaiable <br/>
 *         =0 - The TLS tunnel is establishing but not ready yet <br/>
 *         &lt;0 - The TLS tunnel can not be established
 **/
static inline int _ensure_connect(_handle_t* handle)
{
	if(handle->tls->state == _TLS_STATE_CONNECTING)
	{
		/* Then do the connect! */
		_clear_ssl_error();
		int rc = SSL_accept(handle->tls->ssl);
		if(rc <= 0)
		{
			int reason = SSL_get_error(handle->tls->ssl, rc);
			switch(reason)
			{
				case SSL_ERROR_WANT_READ:
				case SSL_ERROR_WANT_WRITE:
				    LOG_DEBUG("Read/Write failure encountered, deactivate the connection until it gets ready");
				    return 0;
				default:
				    LOG_ERROR("TLS return unexpected error reason: %d, rc: %d, error: %s", reason, rc, ERR_error_string(ERR_get_error(), NULL));
				    return ERROR_CODE(int);
			}
		}
		else if(rc == 1)
		{
			handle->tls->state = _TLS_STATE_CONNECTED;
			LOG_TRACE("TLS Tunnel has been established!");
			return 1;
		}
	}
	return 1;
}

/**
 * @brief check if the pipe should use TLS
 * @param handle the pipe handle to check
 * @return the check result either 1 or 0
 **/
static inline int _should_encrypt(_handle_t* handle)
{
	if(handle->tls->state == _TLS_STATE_DISABLED) return 0;
	return 1;
}

/**
 * @brief deallocate a pipe handle
 * @param ctx the module context
 * @param pipe the pipe handle
 * @param error if this pipe encoutered an unrecoverable error
 * @param purge if we need to release the transportation layer
 * @return status code
 **/
static int _dealloc(void* __restrict ctx, void* __restrict pipe, int error, int purge)
{
	if(NULL == ctx || NULL == pipe) ERROR_RETURN_LOG(int, "Invalid arguments");

	(void)ctx;
	_handle_t* handle = (_handle_t*)pipe;

	if(purge)
	{
		runtime_api_pipe_flags_t flags = itc_module_get_handle_flags(pipe);
		/* If the handle state is connecting, we need to preserve it anyway */
		if(((handle->tls->state == _TLS_STATE_CONNECTING) || (flags & RUNTIME_API_PIPE_PERSIST)) && !error)
		{
			if(!handle->tls->user_state_to_push && ERROR_CODE(int) == _user_space_state_dispose(handle->tls))
			    ERROR_RETURN_LOG(int, "Cannot dispose the user-space status");
			if(_invoke_pipe_cntl(handle->t_pipe, RUNTIME_API_PIPE_CNTL_OPCODE_PUSH_STATE, handle->tls, _tls_context_decref) == ERROR_CODE(int))
			    ERROR_RETURN_LOG(int, "Cannot push TLS context to the transportation layer pipe");
			else
			    handle->tls->pushed = 1;
		}
		else
		{
			if(_invoke_pipe_cntl(handle->t_pipe, RUNTIME_API_PIPE_CNTL_OPCODE_CLR_FLAG, RUNTIME_API_PIPE_PERSIST) == ERROR_CODE(int))
			    ERROR_RETURN_LOG(int, "Cannot clear the persist flag of the trans_pipe");
			if(!handle->tls->pushed && _tls_context_decref(handle->tls) == ERROR_CODE(int))
			    ERROR_RETURN_LOG(int, "Cannot dispose the TLS context");
		}
	}

	int rc = itc_module_pipe_deallocate(handle->t_pipe);

	return rc;
}

/**
 * @brief read from the the pipe
 * @param ctx the module context
 * @param buffer the data buffer
 * @param bytes_to_read the number of bytes to read to buffer
 * @param in the pipe handle
 * @return the size has been actually read to the buffer, or error code
 **/
static size_t _read(void* __restrict ctx, void* __restrict buffer, size_t bytes_to_read, void* __restrict in)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)in;
	if(handle->type != _HANDLE_TYPE_IN) ERROR_RETURN_LOG(size_t, "Wrong pipe type: Cannot read from a output TLS pipe");

	if(NULL != handle->tls->unread_data)
	{
		size_t ret = 0;
		LOG_DEBUG("The tunnel has unread data previously, read from it first");

		size_t nbytes = bytes_to_read;
		if(nbytes > handle->tls->unread_data_size - handle->tls->unread_data_start)
		    nbytes = handle->tls->unread_data_size - handle->tls->unread_data_start;

		memcpy(buffer, handle->tls->unread_data, nbytes);

		bytes_to_read -= nbytes;
		handle->tls->unread_data_start += (uint32_t)nbytes;
		ret += nbytes;

		if(handle->tls->unread_data_start == handle->tls->unread_data_size)
		{
			free(handle->tls->unread_data);
			handle->tls->unread_data = NULL;
			handle->tls->unread_data_size = 0;
			handle->tls->unread_data_start = 0;
		}

		handle->last_read_size = (uint32_t)ret;

		return ret;
	}

	if(_should_encrypt(handle))
	{
		_clear_ssl_error();
		int con_rc = _ensure_connect(handle);
		if(con_rc == ERROR_CODE(int)) ERROR_RETURN_LOG(size_t, "Cannot establish the TLS tunnel");
		else if(con_rc == 0) return 0;

		int rc = SSL_read(handle->tls->ssl, buffer, (int)bytes_to_read);

		if(rc > 0)
		{
			handle->last_read_size = (uint32_t) rc;
			return (size_t) rc;
		}
		else if(rc == 0)
		{
			handle->last_read_size = 0;
			handle->no_more_input = 1;
			return 0;
		}
		else
		{
			int reason = SSL_get_error(handle->tls->ssl, rc);

			if(reason == SSL_ERROR_WANT_READ)
			{
				LOG_DEBUG("The TLS tunnel is waiting for data");
				return 0;
			}

			handle->last_read_size = 0;
			handle->no_more_input = 1;
			LOG_ERROR("TLS read error: (rc = %u, reason = %d) %s", rc, reason, ERR_error_string(ERR_get_error(), NULL));
			return ERROR_CODE(size_t);
		}

		return 0;
	}
	else return itc_module_pipe_read(buffer, bytes_to_read, handle->t_pipe);
}

/**
 * @brief write data through the TLS module
 * @param ctx the module context
 * @param data the data buffer
 * @param nbytes the nubmer of bytes to write
 * @param out the output pipe handle
 * @return the number of bytes to write or error code
 **/
static size_t _write(void* __restrict ctx, const void* __restrict data, size_t nbytes, void* __restrict out)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)out;
	if(handle->type != _HANDLE_TYPE_OUT) ERROR_RETURN_LOG(size_t, "Wrong pipe type: Cannot write from an input TLS pipe");

	size_t ret = 0;

	for(;nbytes > 0;)
	{
		size_t rc;

		if(_should_encrypt(handle))
		{
			_clear_ssl_error();
			int con_rc = _ensure_connect(handle);
			if(con_rc == ERROR_CODE(int)) ERROR_RETURN_LOG(size_t, "Cannot establish the TLS tunnel");
			else if(con_rc == 0) return 0;

			if(ERROR_CODE(int) == _tls_context_incref(handle->tls))
			    ERROR_RETURN_LOG(size_t, "Cannot increase the reference counter for the TLS context object");

			module_tls_dra_param_t draparam = {
				.ssl = handle->tls->ssl,
				.bio = &handle->tls->out_bio_ctx,
				.dra_counter = &handle->tls->dra_counter,
				.conn = handle->tls
			};
			size_t dra_bytes = module_tls_dra_write_buffer(draparam, data, nbytes);

			if(ERROR_CODE(size_t) == dra_bytes)
			{
				LOG_ERROR("Cannot write to the DRA callback queue");
				rc = ERROR_CODE(size_t);
			}
			else if(0 == dra_bytes)
			{
				LOG_DEBUG("The DRA callback qeueue has rejected all the data, so directly write to SSL cipher");
				int ssl_result = SSL_write(handle->tls->ssl, data, (int)nbytes);

				if(ssl_result < 0) rc = ERROR_CODE(size_t);
				else rc = (size_t)ssl_result;
			}
			else
			{
				LOG_DEBUG("The DRA callback queue accepted %zu bytes", dra_bytes);
				rc = dra_bytes;
			}
		}
		else rc = itc_module_pipe_write(data, nbytes, handle->t_pipe);

		if(rc == 0)
		    ERROR_RETURN_LOG(size_t, "Invalid write return code, transportation layer is disconnected?");
		else if(rc != ERROR_CODE(size_t))
		{
			ret += (size_t)rc;
			nbytes -= (size_t)rc;
			data = ((const char*)data) + rc;
		}
		else if(_should_encrypt(handle))
		    ERROR_RETURN_LOG(size_t, "TLS write error: %s", ERR_error_string(ERR_get_error(), NULL));
		else
		    ERROR_RETURN_LOG(size_t, "Transportation layer pipe error");
	}

	return ret;
}

/**
 * @brief the callback used for the TLS DRA
 * @param ctx the context for the TLS module
 * @param source the data source
 * @param out the pipe handle
 * @return status code
 **/
static inline int _write_callback(void* __restrict ctx, itc_module_data_source_t source, void* __restrict out)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)out;
	if(handle->type != _HANDLE_TYPE_OUT)
	    ERROR_RETURN_LOG(int, "Wrong pipe type: Cannot write from an input TLS pipe");

	if(_should_encrypt(handle))
	{
		if(ERROR_CODE(int) == _tls_context_incref(handle->tls))
		    ERROR_RETURN_LOG(int, "Cannot increase the reference counter for the TLS context object");

		module_tls_dra_param_t draparam = {
			.ssl = handle->tls->ssl,
			.bio = &handle->tls->out_bio_ctx,
			.dra_counter = &handle->tls->dra_counter,
			.conn = handle->tls
		};

		return module_tls_dra_write_callback(draparam, source);
	}
	else return itc_module_pipe_write_data_source(source, NULL, handle->t_pipe);
}

/**
 * @brief check if the buffer contains unread data
 * @param ctx the module context
 * @param pipe the pipe to check
 * @return the checking result or status code
 **/
static int _has_unread(void* __restrict ctx, void* __restrict pipe)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)pipe;

	if(handle->tls->state == _TLS_STATE_CONNECTING)
	    return 1;
	else if(handle->tls->state == _TLS_STATE_DISABLED)
	    return !itc_module_pipe_eof(handle->t_pipe);
	if(handle->type != _HANDLE_TYPE_IN) ERROR_RETURN_LOG(int, "Wrong pipe type: _HANDLE_TYPE_IN expected");

	return !handle->no_more_input;
}

/**
 * @brief push a user-space state to the pipe handle
 * @param ctx the module context
 * @param pipe the pipe handle
 * @param state the state to push
 * @param func the dispose function
 * @return status code
 **/
static int _push_state(void* __restrict ctx, void* __restrict pipe, void* __restrict state, itc_module_state_dispose_func_t func)
{
	(void) ctx;
	_handle_t *handle = (_handle_t*) pipe;

	if(NULL != handle->tls->user_state && handle->tls->user_state != state && _user_space_state_dispose(handle->tls) != ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot dispose the previous user-space state");

	handle->tls->user_state = state;
	handle->tls->dispose_user_state = func;

	handle->tls->user_state_to_push = 1;

	return 0;
}

/**
 * @brief pop the previous pushed state from the pipe handle
 * @param ctx the module context
 * @param pipe the pipe handle
 * @return the previously pushed state
 **/
static void* _pop_state(void* __restrict ctx, void* __restrict pipe)
{
	(void)ctx;
	_handle_t *handle = (_handle_t*) pipe;

	void* ret = handle->tls->user_state;

	return ret;
}

/**
 * @brief the callback function used to cleanup when the thread gets killed
 * @param ctx the TLS context
 * @return nothing
 **/
static void _event_thread_killed(void* __restrict ctx)
{
	_module_context_t* context = (_module_context_t*)ctx;

	itc_module_loop_killed(context->transport_mod);
}

/**
 * @brief get the path of the module instance
 * @param ctx the module context
 * @param buf the buffer used to return the path
 * @param sz the size of the buffer
 * @return the result path or NULL on error
 **/
static const char* _get_path(void* __restrict ctx, char* buf, size_t sz)
{
	_module_context_t* context = (_module_context_t*)ctx;

	if(NULL == itc_module_get_path(context->transport_mod, buf, sz))
	    ERROR_PTR_RETURN_LOG("Cannot get the path to transportation layer module");

	return buf;
}

/**
 * @brief get the flags of the module instance
 * @param ctx the moudle context
 * @return the flags
 **/
static itc_module_flags_t _get_flags(void* __restrict ctx)
{
	(void)ctx;
	return ITC_MODULE_FLAGS_EVENT_LOOP;
}

/**
 * @brief the end of message call
 * @param ctx the module context
 * @param pipe the pipe handle
 * @param buffer the buffer that has the data recently read
 * @param offset the offset of the EOM token
 * @return status code
 **/
static int _eom(void* __restrict ctx, void* __restrict pipe, const char* buffer, size_t offset)
{
	(void)ctx;
	_handle_t* handle = (_handle_t*)pipe;

	if(handle->tls->unread_data != NULL)
	{
		LOG_DEBUG("Returning some bytes inside the unread buffer");
		if(handle->tls->unread_data_size <= handle->tls->unread_data_start)
		    ERROR_RETURN_LOG(int, "Invalid offset");
		handle->tls->unread_data_start = (uint32_t)offset;

		return 0;
	}

	if(handle->last_read_size <= offset) ERROR_RETURN_LOG(int, "Invalid offset");

	if(NULL == (handle->tls->unread_data = (char*)malloc(handle->last_read_size - offset)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate buffer for the unread buffer");

	memcpy(handle->tls->unread_data, buffer + offset, handle->last_read_size - offset);
	handle->tls->unread_data_start = 0;
	handle->tls->unread_data_size = handle->last_read_size - (uint32_t)offset;
	return 0;
}

/**
 * @brief add a new certification to the certification chain
 * @param ctx the ssl context
 * @param filename the certification file
 * @return status code
 **/
static inline int _ssl_cert_chain_append(SSL_CTX* ctx, const char* filename)
{
	FILE*  fp;
	X509*  cert = NULL;
	int    ret = ERROR_CODE(int);
	int    empty = 1;

	if(NULL == (fp = fopen(filename, "r")))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot open cert file %s", filename);

	while(NULL != (cert = PEM_read_X509(fp, NULL, ctx->default_passwd_callback, ctx->default_passwd_callback_userdata)))
	{
		if(SSL_CTX_add_extra_chain_cert(ctx, cert) <= 0)
		    ERROR_LOG_GOTO(RET, "Cannot add extra cert from file %s: %s", filename, ERR_error_string(ERR_get_error(), NULL));
		else cert = NULL;
		empty = 0;
	}

	if(empty) ERROR_LOG_GOTO(RET, "The certification file should contains at least one valid certification: %s", filename);

	LOG_DEBUG("Certification from %s has been successfully added to the certification chain", filename);

	ret = 0;
RET:
	if(fp != NULL) fclose(fp);
	if(cert != NULL) X509_free(cert);
	return ret;
}


#if OPENSSL_VERSION_NUMBER >= 0x10002000L
/**
 * @brief Get the next ALPN protocol in the ALPN protocol list format
 * @param prev The previous protocl
 * @return The next protocol, if its the end of the list, return NULL
 **/
static inline const _alpn_protocol_t* _get_next_protocol(const _alpn_protocol_t* prev)
{
	const _alpn_protocol_t* ret = (const _alpn_protocol_t*)&prev->name[prev->length];
	if(ret->length == 0) return NULL;
	return ret;
}

/**
 * @brief Compare two ALPN protocol descriptor
 * @param left The left protocol
 * @param right The right protocol
 * @return The compare result
 **/
static inline int _alpn_protocol_cmp(const unsigned char* left, const unsigned char* right)
{
	uint8_t size = left[0];
	return memcmp(left, right, size);
}

/**
 * @brief The open SSL callback used for ALPN protocol selection
 * @param ssl The ssl context
 * @param out The out buffer
 * @param outlen The length of the out buffer
 * @param in The input buffer
 * @param inlen The input length
 * @param ctxptr The pointer pointed to the module context
 * @return The result code defined by the OpenSSL
 **/
static inline int _alpn_select_protocol(SSL* ssl, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void* ctxptr)
{
	(void)ssl;
	const _alpn_protocol_t* client_proto_list = (const _alpn_protocol_t*)in;
	_module_context_t* ctx = (_module_context_t*)ctxptr;

	const _alpn_protocol_t* server_proto = ctx->alpn_protos, *client_proto = NULL;
	for(;NULL != server_proto; server_proto = _get_next_protocol(server_proto))
	    for(client_proto = client_proto_list;
	        client_proto != NULL &&
	        (unsigned int)((client_proto->name + client_proto->length) - in) <= inlen;
	        client_proto = _get_next_protocol(client_proto))
	        if(_alpn_protocol_cmp(server_proto->begin, client_proto->begin) == 0)
	        {
		        *out = client_proto->name;
		        *outlen = client_proto->length;
		        return SSL_TLSEXT_ERR_OK;
	        }

	return SSL_TLSEXT_ERR_ALERT_FATAL;
}

/**
 * @brief Setup the ALPN protocol list
 * @param context The context
 * @param origin_list The original ALPN protocol list
 * @return status code
 **/
static inline int _setup_alpn_support(_module_context_t* context, const char* origin_list)
{
	if(NULL != context->alpn_protos)
	{
		free(context->alpn_protos);
		context->alpn_protos = NULL;
	}

	if(strcmp(origin_list, "disabled") == 0)
	{
		SSL_CTX_set_alpn_select_cb(context->ssl_context, NULL, NULL);
		LOG_DEBUG("The TLS ALPN Extension has been disabled");
		return 1;
	}

	size_t size = strlen(origin_list);
	/* Because in the worst case, the original list only contains one protocol, so in this case
	 * we need to allocate one more byte for the size of the protocol string.
	 * In addition we need another trailer 0 */
	uint8_t* protolist = (uint8_t*)malloc(size + 2);
	if(NULL == protolist)
	    ERROR_RETURN_LOG(int, "Cannot allocate memory for the ALPN protocol list");

	_alpn_protocol_t* protobuf = (_alpn_protocol_t*)protolist;
	int escape = 0;
	protobuf->length = 0;

	do {
		if(*origin_list == '\\' && !escape)
		{
			escape = 1;
			continue;
		}
		if(*origin_list != 0 && (escape == 1 || (*origin_list != ' ' && *origin_list != '\t')))
		{
			escape = 0;
			protobuf->name[protobuf->length ++] = (uint8_t)*origin_list;
		}
		else if(protobuf->length > 0)
		{
#ifdef LOG_WARNING
			protobuf->name[protobuf->length] = 0;
			LOG_DEBUG("TLS ALPN Protocol Support: %s", protobuf->name);
			uint32_t i;
			for(i = 0;
			    i < sizeof(_supported_alpn_protos) / sizeof(_supported_alpn_protos[0]) &&
			    strcmp((const char*)protobuf->name, _supported_alpn_protos[i]) != 0; i ++);

			if(i >= sizeof(_supported_alpn_protos) / sizeof(_supported_alpn_protos[0]))
			{
				LOG_WARNING("Unrecognized TLS ALPN Protocol Identifier: %s, The TLS Module may be misconfigured", protobuf->name);
			}
#endif /* LOG_WARNING */
			protobuf = (_alpn_protocol_t*)&protobuf->name[protobuf->length];
			protobuf->length = 0;
		}
	} while(*(origin_list++) != 0);

	context->alpn_protos = (_alpn_protocol_t*)protolist;

	SSL_CTX_set_alpn_select_cb(context->ssl_context, _alpn_select_protocol, context);

	LOG_DEBUG("TLS ALPN is enabled");

	return 1;
}
#endif

/**
 * @brief the callback function to get the property
 * @param ctx the mdoule context
 * @param sym the symbol name
 * @param type the data type
 * @param data the buffer used to return data
 * @return status code
 **/
static itc_module_property_value_t _get_prop(void* __restrict ctx, const char* sym)
{
	itc_module_property_value_t ret = {
		.type = ITC_MODULE_PROPERTY_TYPE_NONE
	};
	_module_context_t* context = (_module_context_t*)ctx;
	if(strcmp(sym, "async_write") == 0)
	{
		ret.type = ITC_MODULE_PROPERTY_TYPE_INT;
		ret.num = context->async_write;
	}
	else if(strcmp(sym, "ssl2") == 0)
	{
		long options = SSL_CTX_get_options(context->ssl_context);
		ret.type = ITC_MODULE_PROPERTY_TYPE_INT;
		ret.num = !(options & SSL_OP_NO_SSLv2);
	}
	else if(strcmp(sym, "ssl3") == 0)
	{
		long options = SSL_CTX_get_options(context->ssl_context);
		ret.type = ITC_MODULE_PROPERTY_TYPE_INT;
		ret.num = !(options & SSL_OP_NO_SSLv3);
	}
	else if(strcmp(sym, "tls1") == 0)
	{
		long options = SSL_CTX_get_options(context->ssl_context);
		ret.type = ITC_MODULE_PROPERTY_TYPE_INT;
		ret.num = !(options & SSL_OP_NO_TLSv1);
	}
	else if(strcmp(sym, "tls1_1") == 0)
	{
		long options = SSL_CTX_get_options(context->ssl_context);
		ret.type = ITC_MODULE_PROPERTY_TYPE_INT;
		ret.num = !(options & SSL_OP_NO_TLSv1_1);
	}
	else if(strcmp(sym, "tls1_2") == 0)
	{
		long options = SSL_CTX_get_options(context->ssl_context);
		ret.type = ITC_MODULE_PROPERTY_TYPE_INT;
		ret.num = !(options & SSL_OP_NO_TLSv1_2);
	}
	/* Other options should be the write-only options */
	return ret;
}

/**
 * @brief the callback function to set the property
 * @param ctx the module context
 * @param sym the symbol name
 * @param type the type of the property
 * @param data the data pointer
 * @note the only one property currently supported is the async_write flag
 * @return the status code
 **/
static int _set_prop(void* __restrict ctx, const char* sym, itc_module_property_value_t value)
{
#define _IS(v) (strcmp(sym, (v)) == 0)
#define _SYMBOL(cond) else if(cond)
	_module_context_t* context = (_module_context_t*)ctx;

	if(value.type == ITC_MODULE_PROPERTY_TYPE_INT)
	{
		long options = 0;
		if(0);
		_SYMBOL(_IS("async_write"))              context->async_write = (value.num != 0);
		_SYMBOL(_IS("ssl2") && 0 == value.num)   options |= SSL_OP_NO_SSLv2;
		_SYMBOL(_IS("ssl3") && 0 == value.num)   options |= SSL_OP_NO_SSLv3;
		_SYMBOL(_IS("tls1") && 0 == value.num)   options |= SSL_OP_NO_TLSv1;
		_SYMBOL(_IS("tls1_1") && 0 == value.num) options |= SSL_OP_NO_TLSv1_1;
		_SYMBOL(_IS("tls1_2") && 0 == value.num) options |= SSL_OP_NO_TLSv1_2;
		else return 0;

		if(options != 0 &&  SSL_CTX_set_options(context->ssl_context, options) <= 0)
		    ERROR_RETURN_LOG(int, "Cannot set the supported SSL version: %s", ERR_error_string(ERR_get_error(), NULL));

		return 1;
	}
	else if(value.type == ITC_MODULE_PROPERTY_TYPE_STRING)
	{
		if(0);
		_SYMBOL(_IS("cipher"))
		{
			if(0 == SSL_CTX_set_cipher_list(context->ssl_context, value.str))
			    ERROR_RETURN_LOG(int, "Cipher string %s could be accepted: %s", value.str, ERR_error_string(ERR_get_error(), NULL));
			return 1;
		}
		_SYMBOL(_IS("extra_cert_chain"))
		{
			static char filename[PATH_MAX];
			const char* ch = value.str;
			uint32_t len = 0;
			for(;; ch ++)
			{
				if(*ch != ':' && *ch != 0)
				{
					filename[len++] = *ch;
				}
				else
				{
					filename[len] = 0;
					if(ERROR_CODE(int) == _ssl_cert_chain_append(context->ssl_context, filename))
					    ERROR_RETURN_LOG(int, "Cannot load the certification chain from %s", filename);
					else
					    LOG_DEBUG("Certification %s has been successfully added to the chain", filename);

					if(*ch == 0) break;

					len = 0;
				}
			}
			return 1;
		}
		_SYMBOL(_IS("dhparam"))
		{
			const char* filename = value.str;
			FILE* fp = fopen(filename, "r");
			if(NULL == fp)
			    ERROR_RETURN_LOG_ERRNO(int, "Cannot load dhparam file %s", filename);
			DH* dh = PEM_read_DHparams(fp, NULL, context->ssl_context->default_passwd_callback, context->ssl_context->default_passwd_callback_userdata);
			if(NULL == dh)
			    ERROR_LOG_GOTO(DHPARAM_ERR, "Cannot read the DH params from file %s: %s", filename, ERR_error_string(ERR_get_error(), NULL));

			if(SSL_CTX_set_tmp_dh(context->ssl_context, dh) <= 0)
			    ERROR_LOG_GOTO(DHPARAM_ERR, "Cannot set the DH param to the SSL context %s: %s", filename, ERR_error_string(ERR_get_error(), NULL));

			fclose(fp);
			DH_free(dh);

			return 1;
DHPARAM_ERR:
			if(NULL != fp) fclose(fp);
			if(NULL != dh) DH_free(dh);
			return ERROR_CODE(int);
		}
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
		_SYMBOL(_IS("ecdh_curve"))
		{
			const char* name = value.str;
			if(strcmp(name, "auto") == 0)
			{
				if(SSL_CTX_set_ecdh_auto(context->ssl_context, 1) <= 0)
				    ERROR_RETURN_LOG(int, "Cannot set the ECDH mode to auto %s", ERR_error_string(ERR_get_error(), NULL));

				return 1;
			}
			int nid = OBJ_sn2nid(name);
			if(nid <= 0)
			    ERROR_RETURN_LOG(int, "Cannot get the NID for curve %s: %s", name, ERR_error_string(ERR_get_error(), NULL));

			EC_KEY* ecdh = EC_KEY_new_by_curve_name(nid);
			if(NULL == ecdh)
			    ERROR_RETURN_LOG(int, "Cannot get the curve named %s: %s", name, ERR_error_string(ERR_get_error(), NULL));

			if(SSL_CTX_set_tmp_ecdh(context->ssl_context, ecdh) <= 0)
			{
				EC_KEY_free(ecdh);
				ERROR_RETURN_LOG(int, "Cannot set the context to use curve %s: %s", name, ERR_error_string(ERR_get_error(), NULL));
			}

			return 1;
		}
		_SYMBOL(_IS("alpn_protos"))
		{
			const char* origin_list = value.str;
			if(ERROR_CODE(int) == _setup_alpn_support(context, origin_list))
			    return ERROR_CODE(int);
			return 1;
		}
#endif
		else return 0;
	}
	else return 0;
	return 0;
#undef _SYMBOL
#undef _IS
}


static int  _cntl(void* __restrict context, void* __restrict pipe, uint32_t opcode, va_list va_args)
{
	(void)context;
	_handle_t* handle = (_handle_t*)pipe;

	switch(opcode)
	{
		case MODULE_TLS_CNTL_ENCRYPTION:
		{
			int value = va_arg(va_args, int);
			if(value == 0)
			{
				LOG_DEBUG("Trun off encryption");
				/* Only change the state when the state is connecting, because
				 * Either disabled or connected we do not need to do anything */
				if(handle->tls->state == _TLS_STATE_CONNECTING)
				    handle->tls->state = _TLS_STATE_DISABLED;
			}
			else
			{
				LOG_DEBUG("Turn on encryption");
				/* Only change when the state is disabled */
				if(handle->tls->state == _TLS_STATE_DISABLED)
				    handle->tls->state = _TLS_STATE_CONNECTING;
			}
			break;
		}
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
		case MODULE_TLS_CNTL_ALPNPROTO:
		{
			char*  buf = va_arg(va_args, char*);
			size_t  bs  = va_arg(va_args, size_t);

			if(NULL == buf || bs == 0) ERROR_RETURN_LOG(int, "Invalid arguments");

			uint8_t const* intbuf;
			unsigned int  intsize;

			SSL_get0_alpn_selected(handle->tls->ssl, &intbuf, &intsize);

			if(intsize == 0) buf[0] = 0;
			else
			{
				if(bs > intsize + 1) bs = intsize + 1;
				memcpy(buf, intbuf, bs - 1);
				buf[bs-1] = 0;
			}

			break;
		}
#endif
		default:
		    ERROR_RETURN_LOG(int, "Invalid opcode");
	}
	return 0;
}

int module_tls_module_conn_data_release(module_tls_module_conn_data_t* ctx)
{
	if(NULL == ctx) ERROR_RETURN_LOG(int, "Invalid arguments");
	return _tls_context_decref(ctx);
}

/**
 * @brief the module definition
 **/
itc_module_t module_tls_module_def = {
	.mod_prefix = MODULE_TLS_API_MODULE_PREFIX,
	.handle_size = sizeof(_handle_t),
	.context_size = sizeof(_module_context_t),
	.module_init = _init,
	.module_cleanup = _cleanup,
	.accept = _accept,
	.deallocate = _dealloc,
	.read = _read,
	.write = _write,
	.write_callback = _write_callback,
	.eom = _eom,
	.has_unread_data = _has_unread,
	.push_state = _push_state,
	.pop_state = _pop_state,
	.event_thread_killed = _event_thread_killed,
	.get_path = _get_path,
	.get_flags = _get_flags,
	.get_property = _get_prop,
	.set_property = _set_prop,
	.cntl = _cntl
};
#endif
