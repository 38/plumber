/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The module and pipe manipulation utils
 * @details This file is the actual abstraction layer of the module.
 *         The code which uses the function defined in this file, only need to
 *         know the module instance id/module type code and the module implementation
 *         is hidden behind this. <br/>
 *         The module instance code is managed by the module instance table. <br/>
 *         In this file, each pipe should be associated with a module instance, so that
 *         we can perform operations on the pipe.
 * @file itc/module.h
 **/
#ifndef __PLUMBER_ITC_MODULE__
#define __PLUMBER_ITC_MODULE__
/**
 * @brief the module-pipe handle is the data structure used to store the internal
 *        state of a pipe
 **/
typedef struct _itc_module_pipe_t itc_module_pipe_t;

/**
 * @brief the type for the module type code / module instance id
 * @details The module type code is the legacy name of the module instance identifier <br/>
 *         This is a historical reason for the name, since when the module runtime subsystem
 *         is ready, there's no support for multiple instantiation. <br/>
 *         So each module instance have exactly one module binary, so we used not to distinguish
 *         the module instance and module binary. However, when we introduced the multiple instantiation,
 *         The name of module type is ambigious, and in this case we mean the module instance type.
 **/
typedef uint8_t itc_module_type_t;
STATIC_ASSERTION_EQ_ID(__itc_module_type_size_check__, (itc_module_type_t)-1, 0xff);

/**
 * @brief This is a data view for the pipe handle, and expose the owner information to the upper level code
 **/
typedef struct {
	void* owner;   /*!< the pointer to the owner(point to a task) of this pipe */
} itc_module_pipe_ownership_t;

/**
 * @brief the pipe allocation parameters. This is used when a module is created, what kinds of flags it should have
 *        Also, it accepts an additional arguments which allow higher level code passing an additional param to the
 *        module for the pipe creation.
 * @note  Do not use args too often, and ANY NORMAL PIPE SHOULD NOT USE THIS. <br/>
 *        Because the data is module specified, which means unless the high-level code knows what kinds of module
 *        it dealing with, otherwise there is a protential risk on how the module interpret your additional param. <br/>
 *        Normally it should not be used, because we typically do not expose module specified implementation to higher
 *        level code.
 *        One example of use of this param is to initialize a file pipe, and this is use for testing. <br/>
 **/
typedef struct {
	runtime_api_pipe_flags_t input_flags;	/*!< the input flags */
	runtime_api_pipe_flags_t output_flags;  /*!< the output flags */
	size_t                   input_header;  /*!< the typed header size for the input pipe */
	size_t                   output_header; /*!< the typed header size for the output pipe */
	const void*              args;          /*!< the additional arguments */
} itc_module_pipe_param_t;


/**
 * @brief intialize the pipe module subsystem
 * @return status code
 **/
int itc_module_init();

/**
 * @brief finalize the pipe module subsystem
 * @return status code
 **/
int itc_module_finalize();

/**
 * @brief allocate a new pipe
 * @param type the type of this pipe
 * @param hint the hint number, this is originally desgined for the module code
 *        to predict how the pipe is used, so that it can do some optimization
 *        based on the previous behavior of the servlet. <br/>
 *        This is designed as an identifier of the pipe used in a particular senario.
 *        This parameter is not currently used, but reserved for later use.
 * @param param the pipe parameters
 * @param in_pipe the buffer to return the input pipe handle
 * @param out_pipe the buffer to return the output pipe handle
 * @return status code
 **/
int itc_module_pipe_allocate(itc_module_type_t type, uint32_t hint,
                             const itc_module_pipe_param_t param,
                             itc_module_pipe_t** out_pipe, itc_module_pipe_t** in_pipe);

/**
 * @brief deallocate the pipe, dispose all the resources used by the pipe
 * @param handle the reference to the pipe
 * @return the status code
 **/
int itc_module_pipe_deallocate(itc_module_pipe_t* handle);

/**
 * @brief read number of bytes from the pipe
 * @param nbytes the number of bytes to read
 * @param buffer the buffer for the result
 * @param handle reference to the pipe
 * @note  In the strong typed pipe line system, each pipe have a typed header, once this function
 *        is called, the typed header will be silently swallowed, and there's no way to retrive them
 *        anymore.
 * @return the number of bytes has been read from the pipe, or error code <br/>
 *         If the function returns a nonzero value, which means we have put some
 *         bytes in buffer <br/>
 *         When the data is currently not avaliable, or the end of the data stream has
 *         seen, this function will return zero <br/>
 *         Only in IO error cases, for example a read exception, etc, the function will
 *         return an error code. <br/>
 *         When the function returns 0, it's unable to distinguish if this is caused by the
 *         data is not ready (for example, read from a slow TCP connection).
 *         To realiably detect the end of the data stream, you should use the \a itc_module_pipe_eof
 **/
size_t itc_module_pipe_read(void* buffer, size_t nbytes, itc_module_pipe_t* handle);

/**
 * @brief write bytes to a pipe
 * @param data the data to write
 * @param nbytes the length of the data section
 * @param handle the reference to the pipe
 * @return the number of bytes has been write to the pipe, or error code <br/>
 *         0 indicates nothing has been written (this is possbile, seek the PIPE_ASYNC flag for details) <br/>
 *         It's possible that the function doesn't accept all the bytes provided to the pipe, and in this case,
 *         the caller should make another call to itc_module_pipe_write for the remaining part of the data until
 *         all the data is properly written to the pipe <br/>
 *         If 0 is returned, for example, on the TCP pipe, at most of the time it means the socket is not ready yet. <br/>
 *         In this case you have to make another itc_module_pipe_write call for the data again and again until it accept any bytes.
 *         This cause a performance problem when the connection is slow, make it's very likely to return 0 at most of the time,
 *         to avoid this problem, you need to use the PIPE_ASYNC flag on the output pipe.
 **/
size_t itc_module_pipe_write(const void* data, size_t nbytes, itc_module_pipe_t* handle);


/**
 * @brief write the data source callback function to the given pipe
 * @details This is the data source callback version of the DRA, unlike the write_scope_token which takes a
 *          scope token as the data source. This will takes a data source object directly. <br/>
 *          The scope token is provided for the user-space code, which have no idea about what data source object
 *          is. <br/>
 *          However, in the module code, we may want to wrap a data source to another data source, in this case,
 *          we need this version, because the wrapper data source is not actually in the RLS.
 * @note    besides taking the data source object rather than RLS token, the function is identical with itc_module_pipe_write_scope_token. <br/>
 * @param   data_source the data source callback object we want to write
 * @param   data_req the data request object
 * @param   handle the target pipe's handle
 * @return  The number of callbacks that has been taken by the module, the error code with owership transferring is possible <br/>
 *          1 if the pipe has been taken care of by the module, which means the caller should not close it. <br/>
 *          0 if the pipe is exhuasted before actually send to the module's callback handler, wihch means the caller should close it after the call <br/>
 *          error code on all other error cases, in this case, caller should close the stream properly, because the ownership won't be taken <br/>
 *          error code with ownership transferring is possible, which means the data source has been disposed already and the caller shouldn't dispose it again <br/>
 **/
int itc_module_pipe_write_data_source(itc_module_data_source_t data_source, const runtime_api_scope_token_data_request_t* data_req, itc_module_pipe_t* handle);

/**
 * @brief write the content of the scope token to the give pipe
 * @details In this function, the token has to have the byte stream interface implemented. <br/>
 *         This is the function which we used to represent the semantics for directly write the
 *         content of a scope entity to the pipe. <br/>
 *         For example, in the file server, actually no servlet needs the file content, by using
 *         this function, we can avoid the servlet copying the file content around, which is expensive.
 * @note   the convention of this write function is different, it will guarentee the scope token content
 *         will be completely written to the pipe. So for the non-async write call, it will be a blocking
 *         call, and for async write pipe, the future write request will be pending until this token is finished
 *         to be written. <br/>
 *         This makes the caller do not need to know how many bytes has to written. Also, it's difficult to know that
 *         before the token has been completed written, for async pipe, the function will return before the write operation
 *         completed. <br/>
 *         This function will try to call the module call write_callback, if the target module do not provide this feature,
 *         the function will call the itc_module_pipe_write again and again util all the content has been written to pipe
 * @param  token the token to write
 * @param  handle the handle to the pipe
 * @param  data_req the data request, see the type documentation for the use case
 * @return status code
 **/
int itc_module_pipe_write_scope_token(runtime_api_scope_token_t token, const runtime_api_scope_token_data_request_t* data_req, itc_module_pipe_t* handle);

/**
 * @brief accept a new request from the module. This will block the thread. <br/>
 * @details this function will invoke the module's accept callback, the module's accept callback should
 *         block the caller thread until there's any incoming event which is in ready state.
 * @param type the module instance type
 * @param in_pipe the buffer to return the input pipe handle
 * @param out_pipe the buffer to return the output pipe handle
 * @param param the pipe parameters
 * @return the number of requests is accepted, or a negative error code
 * @note The return could be <br/>
 *       1 if it sucecssfully get an incoming request <br/>
 *       0 if the module is not supported listening <br/>
 *       error_code if the module is support listening but an error happened during listening <br/>
 **/
int itc_module_pipe_accept(itc_module_type_t type, itc_module_pipe_param_t param, itc_module_pipe_t** in_pipe, itc_module_pipe_t** out_pipe);


/**
 * @brief return if this input pipe has no more data in it
 * @param handle the target pipe handle
 * @return &gt; 0 if the pipe reaches the EOF <br/>
 *         = 0 if the pipe has bytes unread or the framework is not currently sure about this<br/>
 *         error code, indicates error cases
 **/
int itc_module_pipe_eof(itc_module_pipe_t* handle);

/**
 * @brief the pipe control function. This is the function that use to control the pipe behaviors
 *        see the opcode documentations for the details
 * @param handle the target pipe
 * @param opcode what kinds of operation to perform, the opcode is define in runtime/api.h
 * @param ap the va list
 * @return the status code
 **/
int itc_module_pipe_cntl(itc_module_pipe_t* handle, uint32_t opcode, va_list ap);


/**
 * @brief get all the module type that can accept events
 * @details The definition of the event accepting module is the module instnace that implememented the
 *         accept module function call and has ITC_MODULE_FLAGS_EVENT_LOOP flags (which means it's not
 *         a slave of other module)
 * @note this function allocates new memory for result, the caller have to free the array after use
 * @return the array to the list, NULL indicates error
 **/
itc_module_type_t* itc_module_get_event_accepting_modules();

/**
 * @brief called when the event loop get killed, this function will invoke the module's
 *        callback for event loop gets killed, and in this function we can release the global
 *        state and context.
 * @param module the module type
 * @return the status code
 **/
int itc_module_loop_killed(itc_module_type_t module);

/**
 * @brief get the human readable name for a module
 * @note if the buffer param is NULL, the default buffer will be used.
 *       This is used in the case that the logging message has been striped,
 *       so if we use a buffer from the caller, the compiler will have an error
 *       for unused varaiable
 * @param module the target module type id
 * @param buffer the memory used to store the name of the module
 * @param size the size of the buffer
 * @return the name of the given module, NULL on error case
 **/
const char* itc_module_get_name(itc_module_type_t module, char* buffer, size_t size);

/**
 * @brief fork an existing pipe
 * @note This is the implementation of the shadow pipes <br/>
 *       The fork function will create an additional output end of the pipe which
 *       shares the same content with its companion. Please note that all the pipes
 *       that is forked is input pipe (because the dehaviour of forking an output
 *       pipe is not well defined) <br/>
 *       The source handle can be either the input side or the output side of the pair
 *       of pipe handle.
 * @param handle the source pipe handle
 * @param pipe_flags the flags used to create this pipe
 * @param header_size the size of the typed header of this pipe
 * @param args the additional arguments pass the the module
 * @return the newly forked pipe handle, NULL on error
 **/
itc_module_pipe_t* itc_module_pipe_fork(itc_module_pipe_t* handle, runtime_api_pipe_flags_t pipe_flags, size_t header_size, const void* args);

/**
 * @brief check if the pipe is a shadow pipe, see the documentation for the RUNTIME_API_PIPE_SHADOW for the detailed concept for
 *        a shadow pipe
 * @param handle the pipe handle
 * @return the result or error code
 **/
int itc_module_is_pipe_shadow(const itc_module_pipe_t* handle);

/**
 * @brief check if the pipe is an input pipe
 * @param handle the pipe to check
 * @return the result or error code
 **/
int itc_module_is_pipe_input(const itc_module_pipe_t* handle);

/**
 * @brief check if the pipe is cancelled
 * @param handle the handle to check
 * @return the result or error code
 **/
int itc_module_is_pipe_cancelled(const itc_module_pipe_t* handle);

/**
 * @brief get the context of the module
 * @note this is a function only used for testing, normal code should not use the module context in this way
 * @return context
 **/
void* itc_module_get_context(itc_module_type_t type);

/**
 * @brief get the module flags from the module identified by the module instance id
 * @param type the module instance id
 * @return the result module flags, or error code
 **/
itc_module_flags_t itc_module_get_flags(itc_module_type_t type);

/**
 * @brief get the path from the module instance id
 * @param module the module type code
 * @param buffer the result buffer
 * @param size the size of the buffer
 * @return the result module path, or NULL when there's no such module/there's any error
 **/
const char* itc_module_get_path(itc_module_type_t module, char* buffer, size_t size);

/**
 * @brief call the on exit module call if the module defined one
 * @param module the moudle type code
 * @return status code
 **/
int itc_module_on_exit(itc_module_type_t module);
/**
 * @brief Set the error flag of the pipe
 * @note Originally, the error flag is autoamtically managed by the framework.
 *       But when the servlet returns a failure code, all the output is not
 *       reliable at this point, thus, we want to set all the pipe belongs
 *       to this servlet to error state. That is why we need this function.
 * @param handle The handle to set
 * @return status code
 **/
int itc_module_pipe_set_error(itc_module_pipe_t* handle);

/**
 * @brief Check if the output pipe contains non-zero output
 * @note This only works with output side of the pipe
 * @param handle The handle to check
 * @return check result, or error code
 **/
int itc_module_pipe_touched(const itc_module_pipe_t* handle);

#endif /* __PLUMBER_ITC_MODULE__ */
