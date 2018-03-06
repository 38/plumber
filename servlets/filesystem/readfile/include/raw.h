/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The Raw output context
 * @brief readfile/input/raw.h
 **/
#ifndef __RAW_H__
#define __RAW_H__

/**
 * @brief The raw context
 **/
typedef struct _raw_ctx_t raw_ctx_t;

/**
 * @brief Create a new raw context
 * @param options The options
 * @param type_model The type model
 * @return newly created context
 **/
raw_ctx_t* raw_ctx_new(const options_t* options, pstd_type_model_t* type_model);

/**
 * @brief Run the raw context
 * @param ctx The context
 * @param type_inst The type instance
 * @param path The path
 * @return status code
 **/
int raw_ctx_exec(const raw_ctx_t* ctx, pstd_type_instance_t* type_inst, const char* path);

/**
 * @brief Dispose a used ctx
 * @@param ctx The context
 * @return status code
 **/
int raw_ctx_free(raw_ctx_t* ctx);

#endif
