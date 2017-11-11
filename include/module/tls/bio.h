/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the OpenSSL BIO interface wrapper for a TCP pipe
 * @file module/tls/bio.h
 **/

#include <constants.h>

#if !defined(__PLUMBER_MODULE_TLS_BIO_H__) && MODULE_TLS_ENABLED
#define __PLUMBER_MODULE_TLS_BIO_H__
#define MODULE_TLS_BIO_TYPE (0x7f000000ull | BIO_TYPE_SOURCE_SINK)

/**
 * @brief   Represent the internal state of the transportation layer BIO
 * @details In order to have the ability to performe real DRA (Direct RLS token Access,
 *          see the documentation about module call write_token for details), we need to
 *          not only be able to write the byte streams but also should be able to write the
 *          RLS token. <br/>
 *          This requires us not directly write what ever it get to the transportation layer,
 *          when the DRA is started. <br/>
 *          For example, if we want to write a RLS file object through the TLS module, the TLS
 *          module can not call the transportation module's write module call directly, because
 *          this will convert the DRA to normal IO, and the data will be bufferred in the transporation
 *          layer's buffer. <br/>
 *          So the solution is instead of use write in this case, the TLS module wrap the file RLS as
 *          a DRA helper callback object, and then pass the DRA helper object to transporation layer's
 *          write_token module call. <br/>
 *          When the transportation layer requires data, the callback function for the helper object will
 *          be used to get the next *encrypted* data. Here's how we do this: <br/>
 *
 *              1. We need write the *original* data from the RLS token stream to the TLS context
 *              2. Then the TLS context will write it in BIO (at this time the BIO should be already in write-into-buffer mode)
 *              3. When the buffer is full or there's no data in the helper object, simple reject the write
 *
 *          The write_token function will be able to retur  the buffer that used in write-into-buffer mode to the transportation layer. <br/>
 *
 *          However, there's an problem, because the TLS assumes the data is written in the correct order, which means, unless we finish the
 *          DRA, we should not write another data to the SSL context. Which means we need either reject or buffer the future data.
 *
 *          However, reject causes user space program poll the pipe status, so we need make the buffer object can contains multiple data sections
 *          (either RLS token or plain data). Unless we are currently not doing DRA, we need to write the data to the helper buffer anyway.
 **/
typedef struct {
	itc_module_pipe_t* pipe;    /*!< the transportation layer pipe */
	size_t             bufsize; /*!< the size of the buffer for copy-into-buffer mode */
	char*              buffer;  /*!< the copy-into-buffer mode buffer, if this is NULL then we are in normal state */
} module_tls_bio_context_t;

/**
 * @brief the get the bio method data structure for the BIO
 * @return the bio method object
 **/
BIO_METHOD* module_tls_bio_method(void);

/**
 * @brief Create a new BIO wrapper for the pipe
 * @param context The BIO context
 * @return the result BIO, NULL on error case
 **/
BIO* module_tls_bio_new(module_tls_bio_context_t* context);

#endif /* __PLUMBER_MODULE_TLS_BIO_H__ */
