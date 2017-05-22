/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the hash map data structure
 * @file hashmap.h
 **/
#ifndef __PLUMBER_HASHMAP_H__
#define __PLUMBER_HASHMAP_H__

/**
 * @brief the incomplete type for a hash map
 **/
typedef struct _hashmap_t hashmap_t;

/**
 * @brief the search result
 **/
typedef struct {
	const void* key_data;   /*!< the data for the key */
	size_t      key_size;   /*!< the size of the key */
	const void* val_data;   /*!< the data for the value */
	size_t      val_size;   /*!< the size of the value data */
} hashmap_find_res_t;

/**
 * @brief create a new hash map
 * @param num_slots the number of slots in the hash map
 * @param init_pool the initial pool size
 * @return the newly created hash map or NULL on error
 **/
hashmap_t* hashmap_new(size_t num_slots, size_t init_pool);

/**
 * @brief free a used hash map
 * @param hashmap the hash map
 * @return status code
 **/
int hashmap_free(hashmap_t* hashmap);

/**
 * @brief insert a new element to the hash map
 * @param hashmap the target hash map
 * @param key_data the key data
 * @param key_size the size of the key data
 * @param val_data the value data
 * @param val_size the value size
 * @param override indicates if we want the data be overriden when it alread exists
 * @param result the find result for the node has been insert
 * @return status code
 **/
int hashmap_insert(hashmap_t* hashmap, const void* key_data, size_t key_size,
                   const void* val_data, size_t val_size,
                   hashmap_find_res_t* result, int override);

/**
 * @brief search for a key item in the hash map
 * @param hashmap the target hash map
 * @param key_data the key data
 * @param key_size the size of key data
 * @param result the result buffer
 * @return number of entry has found or error code
 **/
int hashmap_find(const hashmap_t* hashmap, const void* key_data, size_t key_size, hashmap_find_res_t* result);

#endif /*__PLUMBER_HASHMAP_H__*/
