/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief this file includes the shared data structure that is used for framework-servlet communication
 * @file types.h
 **/
#ifndef __API_SLOT_METADATA_H__
#define __API_SLOT_METADATA_H__
#include <api.h>
/** @brief the metadata that is defined in the servlet and used by the framework */
typedef runtime_api_servlet_def_t servlet_def_t;

/** @brief the section pipe desciptor */
typedef runtime_api_pipe_t pipe_t;

/** @brief the function address table that provide by the framework */
typedef runtime_api_address_table_t address_table_t;

/** @brief the type for the pipe flags */
typedef runtime_api_pipe_flags_t pipe_flags_t;

/** @brief represent a RSL token */
typedef runtime_api_scope_token_t scope_token_t;

/** @brief the type for the scope entity */
typedef runtime_api_scope_entity_t scope_entity_t;

/** @brief the type for the token data request */
typedef runtime_api_scope_token_data_request_t scope_token_data_req_t;

/** @brief the type for the type inference callback function */
typedef runtime_api_pipe_type_callback_t pipe_type_callback_t;

/** @brief the type for the async task handle */
typedef runtime_api_async_handle_t async_handle_t;

/** @brief flag indicates that this is an input pipe */
#define PIPE_INPUT RUNTIME_API_PIPE_INPUT

/** @brief flag indicates that is is an output pipe */
#define PIPE_OUTPUT RUNTIME_API_PIPE_OUTPUT

/** @brief flag suggests that the pipe prefer async IO */
#define PIPE_ASYNC RUNTIME_API_PIPE_ASYNC

/** @brief flag indicates this pipe is a shadow of another one */
#define PIPE_SHADOW RUNTIME_API_PIPE_SHADOW

/** @brief flags suggest the module not to release the resource for reuse */
#define PIPE_PERSIST RUNTIME_API_PIPE_PERSIST

/** @brief flag that indicates a shadow is disabled by default */
#define PIPE_DISABLED RUNTIME_API_PIPE_DISABLED

/** @brief the opcode for pipe_cntl to get the flags variable of a pipe */
#define PIPE_CNTL_GET_FLAGS RUNTIME_API_PIPE_CNTL_OPCODE_GET_FLAGS

/** @brief the opcode for pipe_cntl to set a flag for a pipe */
#define PIPE_CNTL_SET_FLAG  RUNTIME_API_PIPE_CNTL_OPCODE_SET_FLAG

/** @brief the opcode for pipe_cntl to clear a flag for a pipe */
#define PIPE_CNTL_CLR_FLAG  RUNTIME_API_PIPE_CNTL_OPCODE_CLR_FLAG

/** @brief the opcode for pipe_cntl to notify there's a end of message token and the user-space servlet is not going to consume bytes beyond that */
#define PIPE_CNTL_EOM RUNTIME_API_PIPE_CNTL_OPCODE_EOM

/** @brief push a state and attach it to a persist resource */
#define PIPE_CNTL_PUSH_STATE RUNTIME_API_PIPE_CNTL_OPCODE_PUSH_STATE

/** @brief pop a state and dettach it from a persist resource */
#define PIPE_CNTL_POP_STATE RUNTIME_API_PIPE_CNTL_OPCODE_POP_STATE

/** @brief invoke a pipe that represents a service module method */
#define PIPE_CNTL_INVOKE RUNTIME_API_PIPE_CNTL_OPCODE_INVOKE

/** @brief read the typed header of the pipe */
#define PIPE_CNTL_READHDR RUNTIME_API_PIPE_CNTL_OPCODE_READHDR

/** @brief write the typed header of the pipe */
#define PIPE_CNTL_WRITEHDR RUNTIME_API_PIPE_CNTL_OPCODE_WRITEHDR

/** @brief Get the module path that operates the given pipe */
#define PIPE_CNTL_MODPATH  RUNTIME_API_PIPE_CNTL_OPCODE_MODPATH

/** @brief no operation */
#define PIPE_CNTL_NOP RUNTIME_API_PIPE_CNTL_OPCODE_NOP

/** @brief The opcode used to set the async task to the wait mode */
#define ASYNC_CNTL_SET_WAIT RUNTIME_API_ASYNC_CNTL_OPCODE_SET_WAIT

/** @brief The opcode used to notify the wait mode async task is completed  */
#define ASYNC_CNTL_NOTIFY_WAIT RUNTIME_API_ASYNC_CNTL_OPCODE_NOTIFY_WAIT

/** @brief The opocde used to get the status code of an async task */
#define ASYNC_CNTL_RETCODE RUNTIME_API_ASYNC_CNTL_OPCODE_RETCODE

/**
 * @brief decide if a pipe is a readable pipe
 * @param f the pipe flags to examine
 * @return the testing result
 **/
#define PIPE_FLAGS_IS_READABLE(f) RUNTIME_API_PIPE_IS_INPUT(f)

/**
 * @brief decide if the pipie is a writable pipe
 * @param f the pipe flags to examine
 * @return the testing result
 **/
#define PIPE_FLAGS_IS_WRITABLE(f) RUNTIME_API_PIPE_IS_OUTPUT(f)

/**
 * @brief get the pipe id from a pipe descriptor
 * @param p the pipe descriptor
 * @return the pipe id
 **/
#define PIPE_GET_ID(p) RUNTIME_API_PIPE_TO_PID(p)

/**
 * @brief get the pipe flag that can be used as a shadow of a pipe
 * @param t the target pipe
 * @return the flag
 **/
#define PIPE_MAKE_SHADOW(t) (PIPE_SHADOW | PIPE_OUTPUT | PIPE_GET_ID(t))

#endif
