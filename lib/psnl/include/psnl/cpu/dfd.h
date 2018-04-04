/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The RLS object for a double spatial field on CPU
 * @file include/psnl/cpu/dfd.h
 **/
#ifndef __PSNL_CPU_DFD_H__
#define __PSNL_CPU_DFD_H__

/**
 * @brief The actual data structure for the CPU field with double precision cells
 **/
typedef struct _psnl_cpu_dfd_t psnl_cpu_dfd_t;

/**
 * @brief Create new N dimensional field
 * @param dim The dimensional data
 * @return The newly created object
 **/
psnl_cpu_dfd_t* psnl_cpu_dfd_new(const psnl_dim_t* dim);

/**
 * @brief Increase the reference counter for this field
 * @param field The target field
 * @return status code
 **/
int psnl_cpu_dfd_incref(const psnl_cpu_dfd_t* field);

/**
 * @brief Decrease the reference counter for this field
 * @param field The target field
 * @return status code
 **/
int psnl_cpu_dfd_decref(const psnl_cpu_dfd_t* field);

/**
 * @brief Commit current field to the RLS
 * @param field The field to commit
 * @return The scope token
 **/
scope_token_t psnl_cpu_dfd_commit(psnl_cpu_dfd_t* field);

/**
 * @brief Get the data from the field object
 * @param field The target field
 * @param dim_buf The dimension buffer
 * @return status code
 **/
double* psnl_cpu_dfd_get_data(psnl_cpu_dfd_t* field, psnl_cpu_dfd_t* dim_buf);

/**
 * @brief Get the read only data pointer from the field object 
 * @param field The target field
 * @param dim_buf The diemension buffer
 * @return status code
 **/
const double* psnl_cpu_dfd_get_data_const(psnl_cpu_dfd_t* field, psnl_cpu_dfd_t* dim_buf);

#endif /* __PSNL_CPU_DFD_H__ */
