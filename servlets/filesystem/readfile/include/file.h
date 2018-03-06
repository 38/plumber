/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The servlet context for the RLS file object
 * @file filesystem/include/file.h
 **/
#ifndef __FILE_H__
#define __FILE_H__

/**
 * @brief The file output contex
 **/
typedef struct _file_ctx_t file_ctx_t;

/**
 * @brief Create a new file context
 * @param options The options 
 * @param type_model The type model
 * @return newly created file context
 **/
file_ctx_t* file_ctx_new(const options_t* options, pstd_type_model_t* type_model);

/**
 * @brief Run the servlet with file mode
 * @pram file_ctx The file context
 * @param type_inst The type instance
 * @param path The path to read
 * @return status code
 **/
int file_ctx_exec(const file_ctx_t* file_ctx, pstd_type_instance_t* type_inst, const char* path);

/**
 * @brief Dispose the file context
 * @param ctx The context
 * @return status code
 **/
int file_ctx_free(file_ctx_t* ctx);
#endif
