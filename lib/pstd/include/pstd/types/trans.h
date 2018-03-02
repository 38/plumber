/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @breif The stream processor which takes another RLS stream and process on the fly.
 *        The token can not be reopened
 * @details This token is typically useful when we want to compress the data on the fly.
 * @file pstd/types/trans.h
 **/
#ifndef __PSTD_STREAMTRANS_H__
#define __PSTD_STREAMTRANS_H__

#include <pstd/scope.h>

/**
 * @brief The RLS stream transformer object
 **/
typedef struct _pstd_trans_t pstd_trans_t;

/**
 * @brief The dummy type stream transformer instance
 * @note The life cycle of a stream prcessor is:
 *        1. When the stream transformation process begins, the init_func callback will be called and returns a transformer instance
 *        2. When the original data is availiable , use the feed_func give it to the transformer
 *        3. Use fetch_func fetch all the processed bytes. If the return value is non-zero, goto step 3.
 *        4. Repeat the step 2, util all data processed
 *        5. cleanup_func
 **/
typedef struct _pstd_trans_inst_t pstd_trans_inst_t;

/**
 * @brief The data structure that is used to describe a data processor
 **/
typedef struct {
	void*     data;         /*!< The additional data for the processor callbacks */
	/**
	 * @brief Initialize the stream processor
	 * @param data The addtional data to pass in
	 * @return The stream processor handle NULL on erro case
	 **/
	pstd_trans_inst_t*  (*init_func)(void* data);

	/**
	 * @brief Feed data to the stream processor
	 * @param stream_proc The stream processor to feed in
	 * @param in The input buffer
	 * @param size The size of input buffer
	 * @note if this function called with a NULL in buffer, it indicates the data source has been exhuasted
	 * @return The number of bytes has been accepted
	 **/
	size_t (*feed_func)(pstd_trans_inst_t* __restrict stream_proc, const void* __restrict in, size_t size);

	/**
	 * @brief Fetch the processed data from the stram processor
	 * @param stream_proc The stream processor to fetch
	 * @param out The output buffer
	 * @param size The size of the buffer
	 * @return Actual bytes that has been read
	 **/
	size_t (*fetch_func)(pstd_trans_inst_t* __restrict stream_proc, void* __restrict out, size_t size);

	/**
	 * @brief Dispose a used stream processor
	 * @param stream_proc The stream processor
	 * @return status code
	 **/
	int (*cleanup_func)(pstd_trans_inst_t* stream_proc);
} pstd_trans_desc_t;

/**
 * @brief Create a new scope token
 * @param desc The stream processor description
 * @param token The RLS token used as data source
 * @return The newly created stream transformer
 **/
pstd_trans_t* pstd_trans_new(scope_token_t token, pstd_trans_desc_t desc);

/**
 * @brief Set the size of read buffer for this stream transformer
 * @param trans The stream transformer to set
 * @param size The new size for the buffer
 * @return status code
 **/
int pstd_trans_set_buffer_size(pstd_trans_t* trans, size_t size);

/**
 * @berif Commit a stream transformer to the RLS
 * @param trans The stream transformer
 * @return  The token for this RLS
 * @note Commit twice should  be an error
 **/
scope_token_t pstd_trans_commit(pstd_trans_t* trans);

/**
 * @brief Dispose a used stream transformer
 * @param trans The stream transformer
 * @return status code
 **/
int pstd_trans_free(pstd_trans_t* trans);

#endif /* __PSTD_STREAMTRANS_H__ */
