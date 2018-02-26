/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The array RLS object
 * @details The RLS array actually manages a list of in-memory blob
 * @todo Currently we don't support copy operation. Question, do we need a deep copy?
 * @file pstd/types/array.h
 **/
#ifndef __PSTD_ARRAY_H__
#define __PSTD_ARRAY_H__

#include <pstd/type.h>
#include <pstd/types/blob.h>

/**
 * @brief The array object
 **/
typedef struct _pstd_array_t pstd_array_t;

/**
 * @brief Create a new array
 * @param blob_model The model for the inner type blob
 * @param init_cap The initial capacity
 * @return The newly created array
 **/
pstd_array_t* pstd_array_new(const pstd_blob_model_t* blob_model, size_t init_cap);

/**
 * @brief Dispose a used array
 * @param array The array object to dispose
 * @note If the array object has been committed to the RLS, the function will reject the operation 
 * @return status code
 **/
int pstd_array_free(pstd_array_t* array);

/**
 * @brief Retrieve an RLS array object from the RLS
 * @param token The token we used to retrieve the RLS
 * @return The array object we retrieved from the RLS
 **/
const pstd_array_t* pstd_array_from_rls(scope_token_t token);

/**
 * @brief Commit an array to the RLS
 * @param array The array to commit
 * @return The RLS token for this array object
 **/
scope_token_t pstd_array_commit(pstd_array_t* array);

/**
 * @brief Get a memory blob from the array
 * @param array The target array object
 * @param idx The index in the array
 * @return The result memory blob, NULL on error cases
 **/
const pstd_blob_t* pstd_array_get(const pstd_array_t* array, uint32_t idx);

/**
 * @brief Get a editable memory blob from the array
 * @param array The target array object
 * @param idx The index in the array
 * @note This function also used to append an element in the array, if this is the case
 *       use (uint32_t)-1 as the index to get a newly allocated memory blob for the next 
 *       element
 * @return the result memory blob, NULL on error cases
 **/
pstd_blob_t* pstd_array_get_mutable(pstd_array_t* array, uint32_t idx);

/**
 * @brief Remove an item from the array
 * @param array The target array object
 * @param idx The index to the element that needs to be removed
 * @return status code
 **/
int pstd_array_remove(pstd_array_t* array, uint32_t idx);

/**
 * @brief Get the size of the RLS array object
 * @param array The array object
 * @return The size of the array or error code
 **/
uint32_t pstd_array_size(const pstd_array_t* array);

#endif
