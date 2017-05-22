/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The request local scope
 * @details The request local scope is the infrastructure that maintains the a local "scope"
 *          for each request, which means, a group of memory buffer that is shared within a
 *          single request.
 * @file sched/rscope.h
 **/
#ifndef __SCHED_RSCOPE_H__
#define __SCHED_RSCOPE_H__

#include <utils/static_assertion.h>

/**
 * @brief the data structure for a request scope
 **/
typedef struct _sched_rscope_t sched_rscope_t;
/**
 * @brief describes the output the copy result for a pointer
 **/
typedef struct {
	runtime_api_scope_token_t token;  /*!< the newly copied token */
	void*                     ptr;    /*!< the pointer to the newly copied memory */
} sched_rscope_copy_result_t;

/**
 * @brief represent an opened RLS pointer byte stream
 **/
typedef struct _sched_rscope_stream_t sched_rscope_stream_t;

/**
 * @brief initialize the request scope global objects
 * @return status code
 **/
int sched_rscope_init();

/**
 * @brief finalize the request scope global objects
 * @return status code
 **/
int sched_rscope_finalize();

/**
 * @brief initialize the thread locals
 * @return status code
 **/
int sched_rscope_init_thread();

/**
 * @brief finalize the thread locals
 * @return status code
 **/
int sched_rscope_finalize_thread();

/**
 * @brief create a new request scope
 * @return the newly created request local scope, NULL on error case
 **/
sched_rscope_t* sched_rscope_new();

/**
 * @brief dispose the used request scope, this will trigger
 *        the disposition of all the managed pointers
 * @param scope the scope to dispose
 * @return status code
 **/
int sched_rscope_free(sched_rscope_t* scope);

/**
 * @brief add a new pointer to the request scope
 * @param scope the request scope
 * @param pointer the pointer
 * @note this function will take the ownership of the pointer->ptr. But the ownership of pointer won't change
 * @return the token for the new pointer that has been added, error code on error case
 **/
runtime_api_scope_token_t sched_rscope_add(sched_rscope_t* scope, const runtime_api_scope_entity_t* pointer);


/**
 * @brief copy the existing pointer to a new pointer
 * @note this function should be used when the caller want to modify the content of the pointer,
 *       This is the implememntation for the Copy-On-Write
 * @param scope the scope where the pointer living
 * @param token the token to copy
 * @param result the copy result
 * @return status code
 **/
int sched_rscope_copy(sched_rscope_t* scope, runtime_api_scope_token_t token, sched_rscope_copy_result_t* result);

/**
 * @brief get the pointer for the given token in the request scope
 * @param scope the scope where we get the pointer from
 * @param token the token we want to get
 * @return the pointer to the memory we are requiring, NULL on error case
 **/
const void* sched_rscope_get(const sched_rscope_t* scope, runtime_api_scope_token_t token);

/**
 * @brief open a byte stream for the given token
 * @details this is the interface for the RLS byte stream representation, see the docs sched_rscope_open_func_t
 *          to learn the details about this concept
 * @param token the token we want to open
 * @return the stream object has been created for the token, NULL on error case
 **/
sched_rscope_stream_t* sched_rscope_stream_open(runtime_api_scope_token_t token);

/**
 * @brief close the byte stream
 * @param stream the stream to close
 * @note close means dispose all the resources that occupied by the stream
 * @return status code
 **/
int sched_rscope_stream_close(sched_rscope_stream_t* stream);

/**
 * @brief check if current position has reached the end-of-stream
 * @param stream the stream to check
 * @return the check result or error code
 **/
int sched_rscope_stream_eos(const sched_rscope_stream_t* stream);

/**
 * @brief read a number of bytes from the stream
 * @param stream the stream to read
 * @param buffer the buffer to read
 * @param count the number of bytes we want to read
 * @return the number of bytes that has been read actually, or error code when error happens
 **/
size_t sched_rscope_stream_read(sched_rscope_stream_t* stream, void* buffer, size_t count);

#endif /* __SCHED_RSCOPE_H__ */
