/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The memory object with a reference counter
 * @file psnl/include/psnl/memobj.h
 **/
#ifndef __PSNL_MEMOBJ_H__
#define __PSNL_MEMOBJ_H__

/**
 * @brief The memory object that is managed by PSNL 
 **/
typedef struct _psnl_memobj_t psnl_memobj_t;

/**
 * @brief The customized dispose function for a memory object
 * @param object The object to dispose
 * @param user_data The user data need to be passed into this callback
 * @return status code
 **/
typedef int (*psnl_memobj_dispose_func_t)(void* object, void* user_data);

/**
 * @brief The customized creation function for a memory object
 * @param user_data the user data needs to be passed into the callback
 * @return The newly created object
 **/
typedef void* (*psnl_memobj_create_func_t)(const void* user_data);

/**
 * @brief The initialization parameters for a managed memory object
 **/
typedef struct {
	uint64_t                     magic;            /*!< The magic number used to inditify the type of the object */
	void*                        obj;              /*!< The actual memory object */
	psnl_memobj_create_func_t    create_cb;        /*!<  The creation callback, either obj or create_cb should be provided, but not both */
	void*                        dispose_cb_data;  /*!< The additional user specific data for the dispose callback functions */
	psnl_memobj_dispose_func_t   dispose_cb;       /*!< The dispose callback, if NULL given, use standard free from libc */
} psnl_memobj_param_t;

/**
 * @brief Create a new managed memory object
 * @param param The memory object parameter
 * @param create_data The additional data pointer passed into the create callback
 * @return The newly created memory object
 **/
psnl_memobj_t* psnl_memobj_new(psnl_memobj_param_t param, const void* create_data);

/**
 * @brief Dispose a used memory object
 * @param memobj The memory object to dispose
 * @note This  function will dispose the memory object wrapper itself and (only if the inner object is not disposed) the inner object
 * @return status code
 **/
int psnl_memobj_free(psnl_memobj_t* memobj);

/**
 * @brief Increase the refrence counter
 * @param memobj The memory object
 * @note eventhough some of the holder just have a reference to this memory object, but it's still needed for the hold to make sure
 *       the memory object is valid
 * @return status code
 **/
int psnl_memobj_incref(const psnl_memobj_t* memobj);

/**
 * @brief Decrease the reference counter
 * @param memobj The memory object
 * @return status code
 **/
int psnl_memobj_decref(const psnl_memobj_t* memobj);

/**
 * @brief Get the memory object as a constant
 * @param memobj The memory object
 * @param magic The magic number
 * @return The pointer to the actual object
 **/
const void* psnl_memobj_get_const(const psnl_memobj_t* memobj, uint64_t magic);

/**
 * @brief Get the memory object 
 * @param meobj The memory object
 * @param magic The magic number
 * @return The pointer to  the actual object
 **/
void* psnl_memobj_get(psnl_memobj_t* memobj, uint64_t magic);

/**
 * @brief Set the flag bit to indicate the memory object has been committed to the RLS
 * @param memobj The memory object to modify
 * @param val The value
 * @return status code
 **/
int psnl_memobj_set_committed(psnl_memobj_t* memobj, int val);

/**
 * @brief Check if the memory object has been committed
 * @param memobj The memory object
 * @return The check result or the error code
 **/
int psnl_memobj_is_committed(const psnl_memobj_t* memobj);

#endif
