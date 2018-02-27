/**
 * Copyright (C) 2017-2018, Hao Hou
 **/
/**
 * @brief The request local scope
 * @details This is the wrapper function for the request local scope infrastructure, which manages the
 *          pointers shared among the tasks of the same request
 * @file pstd/include/pstd/scope.h
 **/
#ifndef __PSTD_SCOPE_H__
#define __PSTD_SCOPE_H__

/**
 * @brief The dummy type that is for compiler knowing we are talking about the RLS stream
 **/
typedef struct _pstd_scope_stream_t pstd_scope_stream_t;

/**
 * @brief add a new pointer to the scope managed pointer infrastructure
 * @details Once this function gets called, the memory will be assigned with an integer token, then the servlet
 *          will be able to write the token to pipe, and the downstream servlet will be able to get the pointer
 *          by the token in the pipe. <br/>
 *          The memory will be automatically disposed after the request is done. <br/>
 *          This function will take the ownership of the pointer mem, so do not dispose the pointer once the function
 *          returns successfully
 * @param entity the scope entity to add
 * @note  the entity parameter do not pass the ownership of the entity, however, entity->data will be taken if
 *        the function retuens successfully
 * @return  the token for the pointer or error code
 **/
scope_token_t pstd_scope_add(const scope_entity_t* entity);

/**
 * @brief Copy the existing token
 * @details this function will make a copy of the existing memory by calling its copy callback, and then assign it with
 *         a new scope token. This should be used if the shared memory needs to be changed, because we want to make sure
 *         that the result shouldn't be related to the servlet execution order. However, if there are two servlets needs
 *         to change the same pointer, it's possible that the previous one will be overridden by the latter one. So we
 *         need to make sure each of the servlet make their own copy before the change. <br/>
 *         And this guareentee the reuslt is not related to the execution order
 * @param  token the token to copy
 * @param  resbuf the result buffer used to return the pointer after the copy
 * @return the token for the copied pointer or error code
 **/
scope_token_t pstd_scope_copy(scope_token_t token, void** resbuf);

/**
 * @brief get the underlying pointer from the scope token
 * @param token the scope token
 * @return the pointer, NULL on error case
 **/
const void* pstd_scope_get(scope_token_t token);

/**
 * @brief Open a RLS scope as a DRA stream
 * @param token The RLS token to open
 * @return The newly create stream object
 **/
pstd_scope_stream_t* pstd_scope_stream_open(scope_token_t token);

/**
 * @brief Read bytes from the RLS stream
 * @param stream The stream to read
 * @param buf The buffer 
 * @param size The size of buffer
 * @return number of bytes has been read
 **/
size_t pstd_scope_stream_read(pstd_scope_stream_t* stream, void* buf, size_t size);

/**
 * @brief Check if the stream has reached the end
 * @param stream The stream to check
 * @param if the stream has reached end or error code
 **/
int pstd_scope_stream_eof(const pstd_scope_stream_t* stream);

/**
 * @brief Close a stream that is no longer used
 * @param stream The stream to close
 * @param status code
 **/
int pstd_scope_stream_close(pstd_scope_stream_t* stream);

/**
 * @brief Get the ready event
 * @param stream The stream to query
 * @param buf The buffer for the result event
 * @return number of event has been returned
 **/
int pstd_scope_stream_ready_event(pstd_scope_stream_t* stream, scope_ready_event_t* buf);

#endif /* __PSTD_SCOPE_H__ */
