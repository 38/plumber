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
 * @brief Create a new input context
 * @param options The servlet options
 * @return The newly created input context
 **/
input_ctx_t* input_ctx_new(const options_t* options);

/**
 * @brief Read path from the input
 * @param input_ctx The input context
 * @param type_inst The type instance
 * @return The path requested
 **/
const char* input_ctx_read_path(const input_ctx_t* input_ctx, pstd_type_instance_t* type_inst);

/**
 * @brief Dispose a input context
 * @param input_ctx The input context
 * @return status code
 **/
int input_ctx_free(input_ctx_t* input_ctx);
#endif
