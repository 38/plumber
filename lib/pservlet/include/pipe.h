/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <unistd.h>
#include <types.h>
#include <utils/static_assertion.h>

#ifndef __PIPE_H__
#define __PIPE_H__

/**
 * @brief the max length of the name of the pipe
 **/
#define PIPE_MAX_NAME 1024

typedef struct {
	uint32_t size;
	uintptr_t __padding__[0];
	pipe_t pipes[0];
} pipe_array_t;
STATIC_ASSERTION_SIZE(pipe_array_t, pipes, 0);
STATIC_ASSERTION_LAST(pipe_array_t, pipes);

/**
 * @brief create a named pipe
 * @param name the name of the pipe section
 * @param flag indicate the pipe flags
 * @param type_expr the type expression for this type, NULL when the type isn't strong typed. Read more type
 *        system details at the documentation for the PDT insert (runtime_pdt_insert)
 * @return the newly defined pipe
 **/
pipe_t pipe_define(const char* name, pipe_flags_t flag, const char* type_expr);

/**
 * @brief create a named pipe with the pattern
 * @param pattern the pattern of the name
 * @param flag the pipe creation flags
 * @return the newly defined pipe
 **/
pipe_t pipe_define_pattern(const char* pattern, pipe_flags_t flag, const char* type_expr, ...);

/**
 * @brief create a pipe list
 * @param pattern the pattern used for pipe name
 * @param flag the pipe flag
 * @param serial_begin the serial number begins
 * @param serial_end the serial number ends
 * @param type_expr the type expression
 * @note in the pattern use charecter '#' represents the current serial number and ## for '#'
 * @return newly created pipe array
 **/
pipe_array_t* pipe_array_new(const char* pattern, pipe_flags_t flag, const char* type_expr, int serial_begin, int serial_end);

/**
 * @brief dispose a pipe array
 * @param array the pipe array
 * @return  the status code
 **/
int pipe_array_free(pipe_array_t* array);

/**
 * @brief get the pipe from the pipe array
 * @param array the pipe array
 * @param n the subscript of the pipe array
 * @return the result pipe or (pipe_t)-1
 **/
static inline pipe_t pipe_array_get(const pipe_array_t* array, uint32_t n)
{
	if(NULL == array || n >= array->size) return (pipe_t)-1;
	return array->pipes[n];
}

/**
 * @brief read data from a given pipe
 * @param pipe the section descriptor that used to read
 * @param nbytes the number of bytes that needs to be read from the pipe
 * @param buffer the buffer which the data will be copied to
 * @return number of bytes has been read or error code
 **/
size_t pipe_read(pipe_t pipe, void* buffer, size_t nbytes);

/**
 * @brief write data to pipe
 * @param pipe the pipe to write
 * @param data data to write
 * @param count number of bytes that needs to be written
 * @return size or error code
 **/
size_t pipe_write(pipe_t pipe, const void* data, size_t count);

/**
 * @brief read the typed header from the pipe
 * @param pipe the pipe to read
 * @param buffer the buffer for the data
 * @param nbytes how many bytes we want to read
 * @return size or error code
 **/
size_t pipe_hdr_read(pipe_t pipe, void* buffer, size_t nbytes);

/**
 * @brief write the typed header to the pipe
 * @param pipe the pipe to write
 * @param buffer the data buffer
 * @param nbytes how many bytes we want to write
 * @return bytes actually written or error code
 **/
size_t pipe_hdr_write(pipe_t pipe, const void* buffer, size_t nbytes);

/**
 * @brief write the scope tokenj to the pipe
 * @details see the documention for the write_callback module call for details
 * @param pipe the pipe to write
 * @param token the token to write
 * @param datareq the data request descriptor, NULL if it's not needed
 * @return status code
 **/
int pipe_write_scope_token(pipe_t pipe, scope_token_t token, const scope_token_data_req_t* datareq);

/**
 * @brief check if this pipe has definitely no data in it
 * @param pipe the pipe id
 * @return error code or the check result
 **/
int pipe_eof(pipe_t pipe);

/**
 * @brief the pipe control function
 * @param pipe the pipe id
 * @param opcode what kinds of operation to perform
 * @return the status code
 **/
int pipe_cntl(pipe_t pipe, uint32_t opcode, ...);

/**
 * @brief get the the module specified module prefix for opcode
 * @param path the target path
 * @param result the result buffer
 * @return the prefix or status code
 **/
int pipe_cntl_mod_prefix(const char* path, uint8_t* result);

/**
 * @brief Set the callback function that will be called after the type of the pipe is determined by the framework
 * @param pipe the pipe to hook
 * @param callback the callback function
 * @param data the data that would be passed into the callback function
 * @return status code
 **/
int pipe_set_type_callback(pipe_t pipe, pipe_type_callback_t callback, void* data);


/**
 * @brief define the getter function for a module specified function\
 * @param path the path for the module definition prefix (for example, pipe.tls)
 * @param opcode the macro name for the opcode
 * @note you can not use a instant number, because the macro actually use the macro name as a part of the getter function name
 **/
#define PIPE_DEFINE_MOD_OPCODE_GETTER(path, opcode) \
    static inline uint32_t _pipe_get_module_specified_opcode_##opcode()\
    {\
	    static uint32_t ret = ERROR_CODE(uint32_t);\
	    if(ret == ERROR_CODE(uint32_t))\
	    {\
		    uint8_t prefix;\
		    if(pipe_cntl_mod_prefix(path, &prefix) == ERROR_CODE(int)) \
		        ERROR_RETURN_LOG(uint32_t, "Cannot get the module prefix");\
		    if(ERROR_CODE(uint8_t) == prefix) return PIPE_CNTL_NOP;\
		    ret = RUNTIME_API_PIPE_CNTL_MOD_OPCODE((uint32_t)prefix, opcode);\
	    }\
	    return ret;\
    }

/**
 * @brief get a module specified opcode
 * @param opcode the opcode macro
 **/
#define PIPE_MOD_OPCODE(opcode) _pipe_get_module_specified_opcode_##opcode()

#endif
