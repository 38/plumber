/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The direct RLS access support
 * @details this is the impmementation of the wrapper callback which
 *         converts the raw callback to encrypted callback
 * @file   module/tls/dra.h
 **/
#include <constants.h>

#if !defined(__PLUMBER_MODULE_TLS_DRA_H__) && MODULE_TLS_ENABLED
#define __PLUMBER_MODULE_TLS_DRA_H__

/**
 * @brief the parameter we used to initialize the DRA
 **/
typedef struct {
	SSL*                            ssl;   /*!< The TLS context */
	module_tls_bio_context_t*       bio;   /*!< The BIO context */
	module_tls_module_conn_data_t*  conn;  /*!< The connection data */
	uint32_t*                 dra_counter; /*!< A pointer to an 32-bit unsigned int, which used keep tracking the number of pending DRA */
} module_tls_dra_param_t;

/**
 * @brief Write a callback using the TLS DRA method
 * @param param the parameter used by the DRA
 * @param source the data source callback to write
 * @note  this function is supposed always writting data source with DRA
 * @return status code, error code with owership transferring is possible, which means
 *         the function has closed the data source already.
 **/
int module_tls_dra_write_callback(module_tls_dra_param_t param, itc_module_data_source_t source);

/**
 * @brief try to write the data buffer to DRA, if there's no undergoing DRA, just do nothing
 * @param param the dra object we need to write
 * @param data the data buffer
 * @param size the size of the data buffer
 * @return the number of bytes has been written to the DRA queue, or error code
 * @note this function do not guareentee the data will be written to the DRA queue and will be consumed by
 *       the DRA buffer. <br/>
 *       If all DRA buffer is previously exhausted, and there's no undergoing DRA, the function will return 0
 *       and this will be the only case that the function will return 0, and reject all the data in the buffer. <br/>
 *       This means the DRA is not activated and we can direcly write the transporation layer <br/>
 *       The thread safety is guareenteed by this function internally. Because once the function returns 0, it means
 *       we do not have any async loop can write this DRA object, so only the worker thread can change the DRA state,
 *       in this case, no lock is required.
 **/
size_t module_tls_dra_write_buffer(module_tls_dra_param_t param, const char* data, size_t size);

/**
 * @brief initialize the static variables in the DRA write callback object
 * @return status code
 **/
int module_tls_dra_init(void);

/**
 * @brief finalize the static variable in the DRA write callback object
 * @return status code
 **/
int module_tls_dra_finalize(void);

#endif /* __PLUMBER_MODULE_TLS_DRA_H__ */
