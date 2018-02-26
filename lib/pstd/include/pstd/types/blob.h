/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The in-memory blob that represents a protocol type
 * @details This is the utilies for all the container types which actually carries multiple
 *          protocol blob in memory.
 *          In the blob we actaully memorize all the underlying object of the scope token, thus
 *          we can eliminate the cost for accessing RLS.
 * @file pstd/types/blob.h
 **/
#ifndef __PSTD_BLOB_H__
#define __PSTD_BLOB_H__

/**
 * @brief The model of the blob based on the type information
 **/
typedef struct _pstd_blob_model_t pstd_blob_model_t;

/**
 * @brief The index of the scope token in the type
 * @note This is used becacause we want to make all the token access faster
 **/
typedef uint32_t pstd_blob_token_idx_t;

/**
 * @brief The actual data structure for the in-meomry blob
 **/
typedef struct _pstd_blob_t pstd_blob_t;

/**
 * @brief Create a new blob model for the given type name
 * @param type_name The type name we want to create the blob model for
 * @return The newly created blob model
 **/
pstd_blob_model_t* pstd_blob_model_new(const char* type_name);

/**
 * @brief Dispose a used blob model
 * @param model The blob model to dispose
 * @return status code
 **/
int pstd_blob_model_free(pstd_blob_model_t* model);

/**
 * @brief Get the number of RLS tokens in the model
 * @param model The blob model to inspect
 * @return The number of token in model or error code
 **/
uint32_t pstd_blob_model_num_tokens(pstd_blob_model_t* model);

/**
 * @brief Get the token index of the model
 * @param model The model of the blob
 * @param field_expr The field expression to the blob
 * @return The token index for this type or error code
 **/
pstd_blob_token_idx_t pstd_blob_model_token_idx(const pstd_blob_model_t* model, const char* field_expr);

/**
 * @brief Get the total memory size of the in-memory blob for this model
 * @param model The model to query
 * @return The memory blob size or error code
 * @note This function will return the size of the blob and the additional data for acceleration (For example RLS object pointer  array)
 **/
size_t pstd_blob_model_full_size(const pstd_blob_model_t* model);

/**
 * @brief Get the size of the data section of the in-memory blob for this model
 * @param model The model to query
 * @return The size of the data section in the memory blob
 * @note This function returns the size without additional data
 **/
size_t pstd_blob_model_data_size(const pstd_blob_model_t* model);


/**
 * @brief Create a new in-memomry blob
 * @param model The model we want to query
 * @param memory If it's not NULL, use the memory address instead of allocate one
 * @return the newly created memory blob
 **/
pstd_blob_t* pstd_blob_new(const pstd_blob_model_t* model, void* memory);

/**
 * @brief Get the actual blob memory pointer
 * @param blob The blob to get
 * @param model The blob model for this type
 * @return The memory pointer to the data section
 **/
void* pstd_blob_get_data(pstd_blob_t* blob);

/**
 * @brief Get the actual blob memory pointer with readonly access permission
 * @param blob The blob to get
 * @return The memory pointer to the data secion
 **/
const void* pstd_blob_get_data_const(const pstd_blob_t* blob);

/**
 * @brief Read a field from the blob
 * @param blob The blob to read
 * @param field the field to read
 * @param data The data buffer used for the result
 * @param bufsize The size of the data buffer
 * @return status code
 **/
int pstd_blob_read(const pstd_blob_t* blob, const pstd_type_field_t* field, void* data, size_t bufsize);

/**
 * @brief Write a field to the blob
 * @param blob The blob to write
 * @param field The field information
 * @param data The data buffer to write
 * @param size The size of the data buffer
 * @return status code
 **/
int pstd_blob_write(pstd_blob_t* blob, const pstd_type_field_t* field, const void* data, size_t size);

/**
 * @brief Read a token from the in memory blob
 * @param blob The blob object
 * @param idx The index of the token
 * @param objbuf If this buffer is not NULL, return the actual object pointer to that object
 * @param blob_model The blob model we want to use
 * @return number of objects has been returned or error code
 **/
int pstd_blob_read_token(const pstd_blob_t* blob, const pstd_blob_model_t* blob_model, pstd_blob_token_idx_t idx, void const * * objbuf);

/**
 * @brief Write a token to the memory blob
 * @param blob The blob to write
 * @param idx The index of of the scope token in the memory blob
 * @param token The actual token value
 * @param obj The object poiner, if given just use the pointer as the actual object pointer
 * @param blob_model The blob model we want to use
 * @note The obj param is entirely for performance consideration, the caller should make sure
 *        the obj is exactly the same object described by the token.
 *        Otherwise the behavior is not well-defined.
 *        We actually enforce this when the FULL_OPTIMIZATION compile flag is off.
 * @return status code
 **/
int pstd_blob_write_token(pstd_blob_t* blob, const pstd_blob_model_t* blob_model, pstd_blob_token_idx_t idx, scope_token_t token, const void* obj);

/**
 * @brief Dispose a used blob
 * @param blob The blob to dispose
 * @note If the blob is created using extranl memory (the memory param is not NULL when it's created)
 * @return status code
 **/
int pstd_blob_free(pstd_blob_t* blob);

#endif /* __TYPES_BLOB_H__ */
