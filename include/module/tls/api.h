/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the API header for TLS module
 **/

#include <constants.h>

#if !defined(__MODULE_TLS_API_H__) && MODULE_TLS_ENABLED
#define __MODULE_TLS_API_H__

/**
 * @brief the module prefix used by TLS module
 **/
#define MODULE_TLS_API_MODULE_PREFIX "pipe.tls"

/**
 * @brief the macro for the encryption switch
 **/
#define MODULE_TLS_CNTL_ENCRYPTION_RAW 0x0

#	ifdef __PSERVLET__
/**
 * @brief define the getter function
 **/
PIPE_DEFINE_MOD_OPCODE_GETTER(MODULE_TLS_API_MODULE_PREFIX, MODULE_TLS_CNTL_ENCRYPTION_RAW);

/**
 * @brief the opcode used by the servlet
 **/
#		define MODULE_TLS_CNTL_ENCRYPTION PIPE_MOD_OPCODE(MODULE_TLS_CNTL_ENCRYPTION_RAW)

#	else /* __PSERVLET__ */

#		define MODULE_TLS_CNTL_ENCRYPTION MODULE_TLS_CNTL_ENCRYPTION_RAW

#	endif /* __PSERVLET__ */

#endif /* __MODULE_TLS_API_H__ */
