/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <pstd/types/ostream.h>
#include <pstd/scope.h>

/**
 * @brief Indicates which kinds of block we are in
 **/
typedef enum {
	_BLOCK_TYPE_PAGE,      /*!< a data page */
	_BLOCK_TYPE_MEMORY,    /*!< a data buffer that passed from the outside */
	_BLOCK_TYPE_STREAM     /*!< another RLS stream */ 
} _block_type_t;

/**
 * @brief The actual page section for a data page
 **/
typedef struct {
	uint32_t   size;          /*!< The actual amount of data we have in the page */
	uint32_t   read;          /*!< The read pointer */
	uintpad_t  __padding__[0];
	char       data[0];       /*!< The actual data section */
} _page_data_t;

/**
 * @brief The actual data for the memory buffer block
 **/
typedef struct {
	int (*free_func)(void*);    /*!< The free function for this memory block */
	size_t size;                /*!< The size of the block */
	size_t read;                /*!< The read pointer */
	void*  data;                /*!< The actual data section */
} _memory_buf_t;

/**
 * @brief The RLS stream 
 **/
typedef struct {
	pstd_scope_stream_t*  stream;  /*!< The actual RLS stream */
} _rls_stream_t;

/**
 * @brief The actual data type for a block
 **/
typedef struct _block_t {
	_block_type_t     type;         /*!< The type of current block */
	struct _block_t*  next;         /*!< The next block */
	uintpad_t __padding__[0];
	_page_data_t      page[0];      /*!< The pointer to the page data */
	_memory_buf_t     memory[0];    /*!< The pointer to the memory buffer data */
	_rls_stream_t     stream[0];    /*!< The pointer to the stream data */
} _block_t;

/**
 * @brief The actual output stream 
 **/
struct _pstd_ostream_t {
	uint32_t    commited:1;    /*!< If this object has been committed */
	uint32_t    opened:1;      /*!< If this object has been opened previously */
	_block_t*   list_begin;    /*!< The block list begin */
	_block_t*   list_end;      /*!< The block list end */
};


