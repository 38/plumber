/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The RLS data object used to carry the libcurl result
 * @file client/include/rls.h
 **/
#ifndef __RLS_H__
#define __RLS_H__

/**
 * @brief The previous definition of the RLS data object
 **/
typedef struct _rls_obj_t rls_obj_t;

/**
 * @brief Create a new RLS data object
 * @param curl_handle The curl handle associated to this object
 * @return The newly created RLS data object 
 * @note The data object returned from this function isn't commited to the RLS yet
 **/
rls_obj_t* rls_obj_new(CURL* curl_handle);

/**
 * @brief Dispose a used RLS data object
 * @param obj The RLS data object
 * @param from_curl Indicates if this free request comes from the libcurl
 * @return status code
 * @note disposing a committed RLS object is considered as an error
 **/
int rls_obj_free(rls_obj_t* obj, int from_curl);

/**
 * @brief Try to write the data to RLS object
 * @note This function is used by libcurl thread and the downstream user should use
 *       DRA for token access
 * @param obj The data object
 * @param data The data to write
 * @param count The size of the data
 * @return If the write has successeed 0 if the curl tranmission should be blocked. 1 on write succ. error code for other cases
 **/
int rls_obj_write(rls_obj_t* obj, const void* data, size_t count);

#endif /* __RLS_H__ */
