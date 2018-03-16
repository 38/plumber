/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The input handler
 * @file readfile/include/input.h
 **/
#ifndef __INPUT_H__
#define __INPUT_H__

/**
 * @brief The input context
 **/
typedef struct _input_ctx_t input_ctx_t;

/**
 * @brief The metadata about the input request
 **/
typedef struct {
	uint32_t     partial:1;    /*!< If we are requesting a partial result */
	uint32_t     content:1;    /*!< If we need to return the entire content */
	uint32_t     disallowed:1; /*!< If the operation is not allowed */
	uint64_t     begin;        /*!< The offset of the begining of the range */
	uint64_t     end;          /*!< The end of the range */
} input_metadata_t;

/**
 * @brief Create a new input context
 * @param options The servlet options
 * @return The newly created input context
 **/
input_ctx_t* input_ctx_new(const options_t* options, pstd_type_model_t* type_model);

/**
 * @brief Read path from the input
 * @param input_ctx The input context
 * @param type_inst The type instance
 * @param buf The path buffer
 * @param buf_size The size of the path buffer
 * @param extname Pointer used to return the beginging of the extension name
 * @return The length of the path string
 **/
size_t input_ctx_read_path(const input_ctx_t* input_ctx, pstd_type_instance_t* type_inst, char* buf, size_t buf_size, char const** extname);

/**
 * @brief Read the metadata from the input
 * @param input_ctx The input context
 * @param type_inst The type instance
 * @param metadata The buffer used to return the metadata
 * @return Number of metadata has returned
 **/
int input_ctx_read_metadata(const input_ctx_t* input_ctx, pstd_type_instance_t* type_inst, input_metadata_t* metadata);

/**
 * @brief Dispose a input context
 * @param input_ctx The input context
 * @return status code
 **/
int input_ctx_free(input_ctx_t* input_ctx);
#endif
