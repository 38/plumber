/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

/**
 * @brief contains the definition for a pipe module implementation
 * @file module_types.h
 * @note this file is designed to be exposed to the pipe module only, do not include this
 *       if the file is not a part of the pipe module
 **/
#include <stdint.h>
#include <utils/static_assertion.h>
#include <runtime/api.h>
#include <constants.h>

#ifndef __PLUMBER_ITC_MODULE_TYPES_H__
#define __PLUMBER_ITC_MODULE_TYPES_H__

/**
 * @brief indicates the type of the module property
 * @details a module property is a configuration of the module instance, the module instance can have
 *         some property registered with a name, for example, the module property TTL(time to live) in
 *         TCP module indicates the maximum time for the connection pool to hold a stalled socket. <br/>
 *         This config can be changed in the Service Script code by the assignment statement <br/>
 *
 *         <code>pipe.tcp.port_80.ttl = 300</code><br/>
 *
 *         will set the TTL of the TCP module listening to the 80 port to 300, which means the connection pool
 *         is allowed to hold an connection for 5min at most, if the stall time is longer that that the connection
 *         will be closed anyway.
 **/
typedef enum {
	ITC_MODULE_PROPERTY_TYPE_ERROR,   /*!< The error case */
	ITC_MODULE_PROPERTY_TYPE_NONE,    /*!< Indicates the module do not have this field */
	ITC_MODULE_PROPERTY_TYPE_INT,     /*!< indicates this is a integer */
	ITC_MODULE_PROPERTY_TYPE_STRING   /*!< indicates this is a string */
} itc_module_property_type_t;

typedef struct {
	itc_module_property_type_t type;
	union {
		int64_t num;
		char*    str;
	};
} itc_module_property_value_t;

/**
 * @brief the flag bits that is used by the module instance, the module flags indicates the property of the module
 *        Currently the only property we used is the ITC_MODULE_FLAGS_EVENT_LOOP, whcih means the module will be able
 *        to accept event and we should start a new event loop for the module
 **/
typedef enum {
	ITC_MODULE_FLAGS_NONE       = 0x0,     /*!< indicates we have nothing in the flag bits */
	ITC_MODULE_FLAGS_EVENT_LOOP = 0x1,     /*!< indicates that we should start a event loop for this module */
	ITC_MODULE_FLAGS_EVENT_EXHUASTED = 0x2  /*!< Indicates this module will not pop up any event for sure */
} itc_module_flags_t;

/**
 * @brief the callback function use to dispose the state variable when the attached resource should be killed
 * @details See the documentation for the push_state module call for the details about the pipe state perservation mechanism.
 **/
typedef int (*itc_module_state_dispose_func_t)(void*);

/**
 * @brief represent a data source that provides data for the write_callback module call
 * @details this struct is used to describe an abstracted data source that provides a byte stream which can be
 *         written to the pipe using the write_callback module call
 **/
typedef struct {
	void*      data_handle;   /*!< the actual data handle, which tracks the state of the stream */
	/**
	 * @brief the read callback, which read a number of bytes to the buffer from the data source
	 * @param handle the data handle
	 * @param buffer the buffer used to return the result
	 * @param count the number of bytes to read
	 * @return the nummber of bytes which has been actually read from the data source, or error code
	 **/
	size_t (*read)(void* __restrict handle, void* __restrict buffer, size_t count);
	/**
	 * @brief check if the data source meets the end-of-stream
	 * @param handle the handle for the data source to check
	 * @return the result, or error code
	 **/
	int    (*eos)(const void* __restrict handle);
	/**
	 * @brief represent how we dispose/close the data source
	 * @param handle the handle to close
	 * @return status code
	 **/
	int    (*close)(void* __restrict handle);
} itc_module_data_source_t;

/**
 * @brief the struct to define a pipe module binary
 * @details a pipe module binary is a group of callback functions which defines all the operations that a moudle can do
 *         This is an abstraction layer so that all the modules can perform the same operations, so that the implementation
 *         details of a moudle is transparent to other part of the system.
 **/
typedef struct {
	/**
	 * @brief initialize the module instance, in this function the module context should be properly initialized
	 * @details the memory for the context is managed by the plumber kernel code, and the memory that is used for
	 *         the context of current module instance will be passed in to the context param. <br/>
	 *         In order to know the size of the module instance context, each module binary should have the field
	 *         context_size set properly. <br/>
	 *         For some module, which doesn't support the multiple instantiation, the context memory is not used,
	 *         because it can use a globa variable instead, in this case, set the context_size to 0, and this will
	 *         make the plumber kernel code pass a NULL pointer as context. <br/>
	 *         For each module instance, we can pass different module instantiation param, for example,
	 *         we can use "insmod" statement in the Service Script to add new module instance to the system.
	 *
	 *             insmod "tcp_pipe 80" // this line will add a TCP module instance which is listening to port 80
	 *             insmod "tcp_pipe 443 --slave" // this line will add a TCP module instance which is listening to port 443, but it will mark itself to non-event accepting mode
	 * @param context the context of this module instance
	 * @param argc the number of the instantiation arguments that is passed to the module
	 * @param argv the array of the instantiation arguments that is passed to the module
	 * @return status code
	 **/
	int   (*module_init)(void* __restrict context, uint32_t argc, char const* __restrict const* __restrict argv);
	/**
	 * @brief clean up the module on exit, in this function you should cleanup all the context occupied resources
	 * @note you don't need to free the memory that is allocated to the context itself, because it will be allocated automatically by the plumber kernel code
	 * @param context the context for this module instance
	 * @return status code
	 **/
	int   (*module_cleanup)(void* __restrict context);
	/**
	 * @brief allocate the pipe
	 * @details this this the function that will create a pair of pipes, which shares the same resources. <br/>
	 *         For example, shared memory is an example of allocated pipe. The memory that is used to share data is the
	 *         resource that is shared by the pair of pipes. And when the output-end of the pipe write some data into the pipe
	 *         it can be read from the input-end of the pair of pipe. <br/>
	 *         This is the abstraction of pipelining, another type of pipe is used as abstraction, accepted pipe pair of a IO request from external source
	 *         (Actually this is the same concept in the event in the event loop). See module function accept to learn more about accepted pipe pairs<br/>
	 *         When the pair of pipe needs to be created, the plumber kernel will allocate a pair of memory buffer, both of the buffer have handle_size bytes <br/>
	 *         This buffer will used as the internal state of a pipe instance, this state is not visible outside of the module code. But the plumber kernel
	 *         call this pointer a handle of the pipe. And the pipe handle is used as the low level representation of a pipe instance. <br/>
	 * @note  because the accept and allocate calls represents different communication model (request vs pipelining), so it's rarely need to define the both
	 *        function in a module.
	 * @param context the context for this module instance
	 * @param hint used to predit how this pipe will be used
	 * @param in_pipe the memory used for the input part of the pipe
	 * @param out_pipe the memory used for the output part of the pipe
	 * @param args the addtional argments passed to the module
	 * @note this template_id should be the node ID in the service definition graph
	 * return the handle used to refer this pipe, NULL indicates there's an error
	 **/
	int (*allocate)(void* __restrict context, uint32_t hint, void* __restrict out_pipe, void* __restrict in_pipe, const void* __restrict args);
	/**
	 * @brief deallocate an used pipe
	 * @details although all pipes are created in pair, but it can be deallocated seperately. The reason why we need to deallocate it seperately is simple,
	 *         Because the owner tasks of each pipe in the pair do not run at the same time. When a task is done, the pipe owned by the dead task also dead. <br/>
	 *         So this means, some of the deallocation call requires the function release the shared resources, but some others not. <br/>
	 *         The param purge is used for this purpose, when the plumber kernel figured out the pipe's underlying resource won't be used anymore, the purge flag will
	 *         be passed into the function. In this case the function should release the resource. <br/>
	 *         Otherwise, it should cleanup the non-shared resource only.
	 *         Important: the pipe handle is managed by the plumber kernel, so never try to free the memory occupied by the pipe handle, it will be taken care of by the
	 *         plumber kernel automatically. <br/>
	 *         The complex case of this funciton is when the persist flag is set to this pipe, and the plumber kernel ask the module to purge the pipe.
	 *         In this case the pipe may be purged as usual, if the module decide to ignore the persist flag (NOTE: the persist flag is only a recommendation, and can be ignored)
	 *         However, if the module decide to preserve the resource, the resource can be transferred to the module's internal pool (like a connection pool in TCP module)
	 *         and wait for the resource to be reactivated again. <br/>
	 *         However, when if the pipe has encounter an unrecoverable error, the persist flag should be definitely ignored and the resource should be released anyway.
	 *         (For example, if a socket has encounter an unrecoverable write error, the persist flag should not be apply to the socket, and socket should be release even
	 *          there's a previously request to keep the connection) <br/>
	 *         The error param is used as this purpose, when the error flag is set, the persist flag should not have any effect and it should release the resource anyway.  <br/>
	 *
	 *         When it's not a purged pipe, the error flag is meaningless, and the value of the error flag is undefined in this case, so do not use the error flag unless the
	 *         plumber kernel ask the module to purge the pipe handle. <br/>
	 *
	 *         If there's any user-space state attached to the pipe resource previously(see the push_state and pop_state module call), the function should dispose the user-space state
	 *         that is previously attached to the pipe (If the state has been re-pushed to the pipe, don't dispose).
	 *         Note, the previously atatched state should not be disposed until the pipe is deallocated, because the state may be used by the user-space program, so we have to prevent
	 *         the state from being disposed until the pipe is dead.
	 *
	 * @param context the context for this module instance
	 * @param handle the pipe reference handle
	 * @param error indicates if this pipe have encountered errors
	 * @param purge indicates if the framework is asking a deep deallocate
	 * @note the difference between deep deallocate and normal deallocate is
	 *       the deep version requires the module to deallocate anything that is
	 *       accociated with it (even the data structure that is shared between pipes)<br/>
	 *		 on the other hand, the normal deallocate only ask module to cleanup the data
	 *		 Note that the error param is only valid when purge = 1, otherwise this is a meaning less
	 *		 param and should be ignored
	 *		 for this handle
	 * @return the status code
	 **/
	int   (*deallocate)(void* __restrict context, void* __restrict handle, int error, int purge);
	/**
	 * @brief read number of bytes from the pipe
	 * @details The behavior of the read function is straightfoward, however, there's a problem with how to determine if the pipe has more data in it.
	 *         When the read function returns an error code, it means it encounter an unrecoverable error, such as the lost of connection, etc. <br/>
	 *         When the read function returns 0, it may be a result of a recoverable error, like data is not currently available, or simply just because
	 *         it has exhuasted the data stream. <br/>
	 *         However, from the return value of the read function, there's no way to determine if this is the end of the stream. The 0 return value may or may
	 *         not means we are reaching the data end. <br/>
	 *         If you want to determine the data end reliably, you should use the function pipe_eof, instead of this function. <br/>
	 * @note the implementation of the read module call should be non-blocking compitible, which means it's not necessarily a non-blocking IO function, however,
	 *       the remaining part of the system will assume the function is performing non-blocking IO, which means the param bytes_to_read is only the size of
	 *       the buffer, and the function may read arbitry number of bytes to the buffer. <br/>
	 *       Which means 0 bytes has been read is not a signal for end of stream. <br/>
	 * @param context the context for this module instance
	 * @param bytes_to_read the number of bytes that the servlet needs
	 * @param buffer the result buffer
	 * @param handle the reference to the pipe instance
	 * @return number of bytes has been read or an negative error code
	 **/
	size_t   (*read)(void* __restrict context, void* __restrict buffer, size_t bytes_to_read, void* __restrict handle);

	/**
	 * @brief   The direct buffer access function
	 * @details The direct buffer access interface provides the application layer code a way to access the pipe handle internal
	 *          buffer directly. This helps the code reduce the number of copies that is needs to consume the pipe data.
	 *          For some cases, the pipe resource might contains more than one IO event and the size of the event is unknown
	 *          unless the data is fully parsed. (For example HTTP requests over persistent connections).
	 *          To avoid the memory copy in this case, we actually doesn't provides the actual size of the returned memory
	 *          region, but an estimation of the size of the returned memory region.
	 *          On the other hand the application layer code might request for the buffer in specific size.
	 *          If the size requirement cannot meet the function returns empty. Otherwise the max_size and min_size will be
	 *          set to the module's estimation of the size of the buffer.
	 * @note    This function may return empty for no reason and it's not required for all the module to implement this.
	 * @param   context The module context
	 * @param   result  The pointer buffer that carries the result
	 * @param   min_size The pointer to the min_size variable
	 * @param   max_size The pointer to the max_size variable
	 * @param   handle The pipe handle
	 * @return  Number of regions that has been returned or error code
	 **/
	int (*get_internal_buf)(void* __restrict context, void const** __restrict result, size_t* __restrict min_size, size_t* __restrict max_size, void* __restrict handle);

	/**
	 * @brief   Release a direct buffer access region
	 * @details This is not required if the module knows the actual size of the region, because the stream pointer can move on.
	 *          How ever if the region's actual size is unknown, this function is required to move on.
	 * @param   context The module context
	 * @param   buffer The pipe buffer to release
	 * @param   actual_size The actual size of the buffer
	 * @param   handle      THe pipe handle
	 * @return  status code
	 **/
	int (*release_internal_buf)(void* __restrict context, void const* __restrict buffer, size_t actual_size, void* __restrict handle);

	/**
	 * @brief write bytes to pipe
	 * @details it's not guareenteed that the function will write all the provided data to the pipe. It will return the number of bytes it
	 *         has been written to the pipe. (Note that 0 return value is a valid return value, which means there's no data has been written
	 *         to the pipe, this may caused by the reason like the socket is not ready, etc.)  <br/>
	 *         And this function may or may not be non-blocking, but the caller CANNOT assume this is a blocking IO function. <br/>
	 *         The error code return code indicates an error, which is unrecoverable. <br/>
	 * @note this should be non-blocking-compitible, see the documentationfor the read module call for the detailed description of the non-blocking-compitibility
	 * @param context the context for this module instance
	 * @param data the data to write
	 * @param bytes_to_write the bytes that need to write
	 * @param handle the handle that reference to the pipe
	 * @return status code
	 **/
	size_t   (*write)(void* __restrict context, const void* __restrict data, size_t bytes_to_write, void* __restrict handle);

	/**
	 * @brief write bytes which is acquired from the given data source to pipe
	 * @details This function is another write function, the difference of this write function is instead of taking the actual
	 *          data, this function takes a data source struct, which is actually a group of callback function that can be used
	 *          as a byte stream. The module will acquire the data from the data source and write it to pipe until the data stream
	 *          ends. <br/>
	 *          Note, because we allow async write, which means the actual write operation is not necessarily to be finished before
	 *          this function ends. So it's possible that the number of bytes that needs to be written to the pipe is still unknown
	 *          when this function returns. (Unless the data source ends then we know how many bytes has been written which is caused
	 *          by this function. So in this call, we do not return the number of bytes has been written. Because the semantics of this function
	 *          is different from the write module call, which means the function is not guareenteed to write the requested amount of
	 *          bytes, and if there are unwritten bytes, it's the caller's responsibility to write it again. This semantics is different,
	 *          which guareentee that all the bytes in the data source has to be written to the pipe, so the number of bytes has been
	 *          written to the pipe is no longer meanful. <br/>
	 *          For the module which write_callback module call is not defined, the ITC module interface will convert a itc_module_write_callback
	 *          call to the ordinary write. So it should be defined whenever it's needed. <br/>
	 *          The ownership of source.handle will be taken by the module instance once the function returns successfully
	 * @param context the context for this module instance
	 * @param source the data source descriptor
	 * @param handle the pipe handle
	 * @return status code, error code with ownership transfer is possible
	 **/
	int   (*write_callback)(void* __restrict context, itc_module_data_source_t source, void* __restrict handle);

	/**
	 * @brief Wait for the incoming event util there's any pair of pipe (a.k.a event) is avaiable and create a pair of pipe handle for the event
	 * @details this is actually very similar to the allocate module call (Actually this is the only two ways to create a pipe instance in Plumber) <br/>
	 *         However, the communication model of those two types of pipe pairs are different. This is the IO Request module, which the input pipe and
	 *         output pipe are not share the same data, but it's a input port which you can read data from it and output port to which you need write data. <br/>
	 *         This is distinguished by the plumber kernel automatically, the module do not need to keep track of the type of the pair.
	 * @param context the context for this module instance
	 * @param args the argument passed to the accept function
	 * @param input the input side of the pipe
	 * @param output the output side of the pipe
	 * @return status code
	 **/
	int   (*accept)(void* __restrict context, const void* __restrict args, void* __restrict input, void* __restrict output);

	/**
	 * @brief check if this pipe has no more data, it return positive number if there may be some more data. <br/>
	 *        And if the pipe is definitely no data to read, it should return 0 <br/>
	 *        And any error case should return error code <br/>
	 * @details Because we can not use the return value from the read module call (0 bytes doesn't means end of stream, because the non-blocking nature of read module call)
	 * @param context the contxt for this module instance
	 * @param pipe the pipe to check
	 * @return &gt; 0 when the pipe contains unread data <br/>
	 *         = 0 when the pipe doesn't contain unread data  <br/>
	 *         or error code
	 **/
	int   (*has_unread_data)(void* __restrict context, void* __restrict pipe);

	/**
	 * @brief the module specified control instructions
	 * @details this is the extension mechanism for the module, which means a module can have the module specified operation, where you can handle the
	 *         module specified funcitonality.
	 * @param context the context for this module instance
	 * @param pipe the target pipe handle
	 * @param opcode the operation code, note, this is actually a pipe specified opcode, which the highest 8 bit module instance id is already stripped by
	 *        the system. <br/>
	 *        For example for module instance with Id 0x01, the user space opcode 0x01000002 will be foward into the cntl module call of module 0x01,
	 *        and the opcode param shouldbe 0x2, because the module id is stripped
	 * @param ap the va_args
	 * @return status code
	 **/
	int  (*cntl)(void* __restrict context, void* __restrict pipe, uint32_t opcode, va_list va_args);

	/**
	 * @brief notify that all of the remaining bytes in the buffer is not belongs to this request.
	 * @details This is important, because consider if a client send two HTTP request at the same time, becuase when the servlet is parsing the first request, it can not know how many
	 *         bytes left in the first message, until it gets the content-length field. However, it's totally possible, that the first servlet's read function gets the data for the both
	 *         request. In this case, the servlet should unread all the data that has is not actually used, so that the unread data can be read again for the next request. <br/>
	 * @note [Deprecating Contract]Originally, it only allows unread the data that is currently in buffer, which means, it only allows to unread the bytes not beyond the last read <br/>
	 *       the reason why we need the buffer param is in encryption module like TLS module, the data is actually get from algorithm other than buffer
	 *       at least from external point of view, there's no where to unread the buffer. In this case, we actually can not know what data has been read
	 *       previously, so in this case the module doesn't know the content of the buffer. <br/>
	 *       So the solution is passing the buffer content as long as the offest, so that the module can know what needs to be unread. <br/>
	 * @todo issue: For some communication protocol, if the EOM token is longer than the last read, we can not determine if it's the EOM token. However, this breaks the
	 *       contract, that the caller should not unread more than the length of the buffer. For example, if the EOM token is "end_of_the_message"<br/>
	 *       The first read get "end_of_" and the second read get"the_message", then it can tell the message is end. However, at this time, you have to unread
	 *       the bytes to the begining to the first read. Which breaks the behavior contract. <br/>
	 *       So the solution is allow the TCP module (which is currently the only module that uses this assumption), put a buffer which unread more bytes than the last read.  <br/>
	 *       And the bytes beyond the current buffer, we should allocate a new buffer to save them <br/>
	 * @param context the context for this module instance
	 * @param pipe the target pipe
	 * @param buffer the read buffer to return
	 * @param offeset the offest of the EOM from the begining of the last read
	 * @return status code
	 **/
	int  (*eom)(void* __restrict context, void* __restrict pipe, const char* buffer, size_t offset);

	/**
	 * @brief push a state variable and attach it to the corresponding resource, the state only can be used by the read-end of the pair of the pipe
	 * @details this module call is used when an accepted pipe has no data to read (the read module call returns 0) <br/>
	 *         Normally the servlet should try again until there's data availible in the pipe.
	 *         However, this solution has performance issue, because it makes the servlet poll the pipe again and again.
	 *         In order to solve this problem, the event-accepting module should support a mechanism called resource attachment.
	 *         When a servet has no data to read, the servet can choose make the servlet persisten and store current servlet state
	 *         in the resource attachment, and then end the task without any output <br/>
	 *         This will cause all the downstream task cancelled. However, when the next time the resource is ready to read, and an
	 *         event is poped up, the previous state attached to this resource can be stored. In this way, the servlet or the working thread
	 *         do not have to wait for the resource be ready. Instead, if there's nothing to do, then the servlet can choose to exit and
	 *         make the plumber kernel start a new task for this request when ever the resource is read. <br/>
	 *         Sometimes, the resource will never be activated again, for example, a slow connection has transferred 20 of 100 bytes of a HTTP
	 *         request, then the resource becomes not ready. The servlet will push the state to the resource, however, if then the connection is
	 *         timed out, there's no incoming data anymore. <br/>
	 *         So the module needs to know how to dispose the state variable, that is why we need the dispose callback at the time we push it into the pipe
	 * @note  when the new state has been pushed, the module should keep the previous state until the pipe is deallocated.
	 *        When the pipe is being deep-deallocated, the deallocation function should call the dispose function. <br/>
	 *        If there's a previously pushed state with the pipe resource, pusing a new state should NOT trigger the disposition of previous state, because it may
	 *        be used util the task is end (same time as the pipe is being deallocated). <br/>
	 *        The deallocate module call is responsible to dispose unused state, and this module call should not dispose the previously existing state.
	 * @param context the context for this module instance
	 * @param pipe the target pipe
	 * @param state the state variable
	 * @param dispose the dispose callback function used when the connection is killed
	 * @return status code
	 **/
	int  (*push_state)(void* __restrict context, void* __restrict pipe, void* __restrict state, itc_module_state_dispose_func_t dispose);

	/**
	 * @brief pop the previously saved state variable and dettach it from the resource, only valid for the read-end of the pair of the pipe
	 * @details this is the function we pop the state variable attached form the resource, the detailed concept of the resource atatchment mechanism, see the documentation for push_state
	 * @param context the context for this module instance
	 * @param pipe the target pipe
	 * @return the prevously saved state or NULL on error
	 **/
	void* (*pop_state)(void* __restrict context, void* __restrict pipe);

	/**
	 * @brief the callback function when event thread gets killed
	 *        in this function, the module should change it's state to killed
	 *        so that we can exit the event loop properly
	 * @param context the context for this module instance
	 * @return nothing
	 **/
	void (*event_thread_killed)(void* context);

	/**
	 * @brief the callback function when the infrastructure is trying to set a module property
	 * @details the detailed description of a property can be found from the documentation for the itc_module_property_type_t
	 * @param context the context for this module instance
	 * @param symbol the symbol of this property, an array of string, NULL terminated
	 * @param value the type of the value
	 * @note the module has to build its own reference for a string, because it's not guareenteed that
	 *       the string data is not going to be deallocated later
	 * @return how many property has been set, or error code
	 **/
	int (*set_property)(void* __restrict context, const char* symbol, itc_module_property_value_t value);

	/**
	 * @brief the callback function when the infrastructure is trying to query a module property
	 * @details the detailed description of a property can be found from the documentation for the itc_module_property_type_t
	 * @param context the context for this module instance
	 * @param symbol the symbol of the target property
	 * @note  The module should make a new copy of string (must be malloced) for the string value
	 * @return The value get from the module
	 **/
	itc_module_property_value_t (*get_property)(void* __restrict context, const char * symbol);

	/**
	 * @brief the callback function when the infrastructure needs to "fork" a new pipe to create a shadow
	 * @details the concept of forking a pipe, is make a reference to a pipe and create a new handle, the handle will contains the identical data
	 *         as the source pipe <br/>
	 *         This is used as the implementation of a shadow pipe, which is the light weight way to copy all the data from one pipe to another <br/>
	 *         The detailed description about the shadow pipe, see the documentation for the RUNTIME_API_PIPE_SHADOW
	 * @param context the context for this module instance
	 * @param dest_data the dest memory for the result pipe handle
	 * @param sour_data the source pipe handle to fork
	 * @param args the additional arguments
	 * @note the sour_data must be the original data source. (For allocated pipe, it's the only output side;
	 *        for accepted pipe it's the only input side)
	 *
	 * @return the status code
	 **/
	int (*fork)(void* __restrict context, void* __restrict dest_data, void* __restrict sour_data, const void* __restrict args);

	/**
	 * @brief get the module path of this module instance and this return value will concatenate with the mod_prefix to make the full module path
	 * @details for example, the TCP module listening to 80 port, it's mod_prefix is "pipe.tcp", and it's get_path result "port_80". So the full path
	 *         is "pipe.tcp.port_80"
	 * @param context the module instance context
	 * @param buffer the buffer used by this function
	 * @param size the size of buffer
	 * @note the buffer param is required, otherwise the function should return NULL and log an error. <br/>
	 *       If the buffer is not large enough to carry the complete path, the path should be truncated to the buffer size (include the \0 in the end)
	 * @return the path string
	 **/
	const char* (*get_path)(void* __restrict context, char* buffer, size_t size);

	/**
	 * @brief return the flags for this module isntance, which describes the module's property
	 * @param context the target context for the instance
	 * @return the flag bits
	 **/
	itc_module_flags_t (*get_flags)(void* __restrict context);

	/**
	 * @brief the callback function that will be called when the service module function is invoked
	 * @details this is the mechanism for a service module, which is a module provide the servlet access to the lower level plumber infrastructure, like
	 *         the meomry pool or global storage system, etc.
	 *         For a service module, each function should have opcode, by using the opcode, the target function can be called
	 * @param context the module instance context
	 * @param opcode the operation code for this function
	 * @return status code
	 **/
	int (*invoke)(void* __restrict context, uint32_t opcode, va_list args);

	/**
	 * @brief get the opcode for the function defined in the service module
	 * @details Because we don't know the opcode of the function that has a given name, so we call the function to get a new name for the function
	 * @param context the module instance context
	 * @param name the name of the function
	 * @return the opcode or error code
	 **/
	uint32_t (*get_opcode)(void* __restrict context, const char* name);

	/**
	 * @brief the module call that will be called when the plumber service is going to exit
	 * @note  Althogh the cleanup callback also gets called at this time, but the semantics of the cleanup call is to dispose all the resources
	 *        occupied by the module context. However, some of the finalization step, like calling user-space on exit callback, requires nothing
	 *        is shutted down at the time when it gets invoked. <br/>
	 *        The reason for why we have this call is, if this function is defined, the framework will guareente the function will be called on
	 *        the system exit plus everything is still working. Which means must before the module cleanup function is called.
	 * @note  context the module context
	 * @return status code
	 **/
	int (*on_exit)(void* __restrict context);

	const char* mod_prefix; /*!< the prefix of the module path */

	size_t handle_size;  /*!< the size of the handle */

	size_t context_size; /*!< the size of the context */
} itc_module_t;

/**
 * @brief get the the module flags
 * @note this function is based on the assumption of the memory layout of the internal ITC handle
 *       See the definition of the itc_module_handle_t for more detials
 * @param handle the target handle
 * @return the flags
 **/
static inline runtime_api_pipe_flags_t itc_module_get_handle_flags(const void* handle)
{
	return *(const runtime_api_pipe_flags_t*)((const uintpad_t*)handle - 1);
}

#endif /* __PLUMBER_ITC_MODULE_TYPES_H__ */
