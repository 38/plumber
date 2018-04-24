/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The RLS object for a spatial field
 * @note This code will just manage the field but it won't actually care about the type of the field. 
 *       Actually, we don't really care about the size of the element here. Because if the caller of this group of functions
 *       doesn't know the cell type, we can't do anything about it anyway.
 * @file include/psnl/cpu/field.h
 **/
#ifndef __PSNL_CPU_FIELD_H__
#define __PSNL_CPU_FIELD_H__

/**
 * @brief The actual data structure for the CPU field with double precision cells
 **/
typedef struct _psnl_cpu_field_t psnl_cpu_field_t;


/**
 * @brief Create new N dimensional field
 * @param dim The dimensional data
 * @param elem_size The element size
 * @return The newly created object
 **/
psnl_cpu_field_t* psnl_cpu_field_new(const psnl_dim_t* dim, size_t elem_size);

/**
 * @brief Dispose a field that haven't been committed yet
 * @param field The field to  dispose
 * @return status code
 **/
int psnl_cpu_field_free(psnl_cpu_field_t* field);

/**
 * @brief Acquire a PSNL field from a RLS object
 * @param token The scope token to acquire
 * @return The field object
 **/
const psnl_cpu_field_t* psnl_cpu_field_from_rls(scope_token_t token);

/**
 * @brief Increase the reference counter for this field
 * @param field The target field
 * @return status code
 **/
int psnl_cpu_field_incref(const psnl_cpu_field_t* field);

/**
 * @brief Decrease the reference counter for this field
 * @param field The target field
 * @return status code
 **/
int psnl_cpu_field_decref(const psnl_cpu_field_t* field);

/**
 * @brief Commit current field to the RLS
 * @param field The field to commit
 * @return The scope token
 **/
scope_token_t psnl_cpu_field_commit(psnl_cpu_field_t* field);

/**
 * @brief Get the data from the field object
 * @param field The target field
 * @param dim_buf The dimension buffer
 * @return status code
 **/
void* psnl_cpu_field_get_data(psnl_cpu_field_t* field, psnl_dim_t const* * dim_buf);

/**
 * @brief Get the read only data pointer from the field object 
 * @param field The target field
 * @param dim_buf The diemension buffer
 * @return status code
 **/
const void* psnl_cpu_field_get_data_const(const psnl_cpu_field_t* field, psnl_dim_t const* * dim_buf);

#endif /* __PSNL_CPU_DFD_H__ */
