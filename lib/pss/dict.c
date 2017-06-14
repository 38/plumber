/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <error.h>

#include <utils/hash/murmurhash3.h>

#include <package_config.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/dict.h>

/**
 * @brief Represent a node in the global storage hash table
 **/
typedef struct _node_t {
	uint64_t         hash[2];   /*!< The 128 bits murmurhash3 */
	char*            key;       /*!< The key string */
	pss_value_t      value;     /*!< The actual value of this node */
	struct _node_t*  next;      /*!< The next node in the linked list */
} _node_t; 

/**
 * @brief The actual data structure for the dictionary
 **/
struct _pss_dict_t {
	_node_t* hash[PSS_DICT_HASH_SIZE];   /*!< The global storage hash size */
};

/**
 * @brief Create a new hash node
 * @param hash The hash code
 * @param key The actual key string
 * @param len The length of the key string (not include the trailing 0)
 * @return The newly created hash node
 **/
static inline _node_t* _node_new(uint64_t hash[2], const char* key, size_t len)
{
	_node_t* ret = (_node_t*)calloc(1, sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new hash node");

	if(NULL == (ret->key = (char*)malloc(len + 1)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the key string");

	memcpy(ret->key, key, len + 1);

	ret->hash[0] = hash[0];
	ret->hash[1] = hash[1];
	
	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->key) free(ret->key);
		free(ret);
	}
	return NULL;
}
/**
 * @brief Dispose a used hash node
 * @param node The node to dispose
 * @return status code
 **/
static inline int _node_free(_node_t* node)
{
	if(NULL == node) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0; 
	if(ERROR_CODE(int) == pss_value_decref(node->value))
	{
		LOG_ERROR("Cannot decref the value");
		rc = ERROR_CODE(int);
	}

	if(NULL != node->key) free(node->key);

	free(node);

	return rc;
}

/**
 * @brief Compute the hash slot from the 128 bit hash code
 * @param hash The 128 bit hash code
 * @return The slot id
 **/
static inline uint32_t _hash_slot(const uint64_t* hash)
{
	static uint64_t k = ((1ull << 63) % PSS_DICT_HASH_SIZE) * 2;
	return (uint32_t)((hash[0] * k + hash[1])%PSS_DICT_HASH_SIZE);
}

/**
 * @brief Find a key from the hash table
 * @param table The hash table to find
 * @param key The key to find
 * @param create Indicates if we want to create a node if it's not exit
 * @return The node we found in the hash table
 **/
static inline _node_t* _hash_find(_node_t ** table, const char* key, int create)
{
	size_t len = strlen(key);
	uint64_t hash[2];

	murmurhash3_128(key, len, 0xf37d543fu, hash);

	uint32_t slot = _hash_slot(hash);

	_node_t* ret;
	for(ret = table[slot]; ret != NULL && (ret->hash[0] != hash[0] || ret->hash[1] != hash[1] || strcmp(ret->key, key) != 0); ret = ret->next);

	if(NULL == ret && create)
	{
		if(NULL == (ret = _node_new(hash, key, len)))
			ERROR_PTR_RETURN_LOG_ERRNO("Cannot create new hash node for global variable %s", key);
		ret->next = table[slot];
		table[slot] = ret;
	}

	return ret;
}

pss_dict_t* pss_dict_new()
{
	pss_dict_t* ret = (pss_dict_t*)calloc(1, sizeof(*ret));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the global storage");

	return ret;
}

int pss_dict_free(pss_dict_t* dict)
{
	if(NULL == dict) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;
	uint32_t i;
#ifdef LOG_DEBUG_ENABLED
	uint32_t max_hash_len = 0;
#endif
	for(i = 0; i < PSS_DICT_HASH_SIZE; i ++)
	{
		_node_t* ptr;
#ifdef LOG_DEBUG_ENABLED
		uint32_t hash_len = 0;
#endif
		for(ptr = dict->hash[i]; ptr != NULL; hash_len ++)
		{
			_node_t* this = ptr;
			ptr = ptr->next;
			if(ERROR_CODE(int) == _node_free(this))
				rc = ERROR_CODE(int);
		}
#ifdef LOG_DEBUG_ENABLED
		if(max_hash_len < hash_len) max_hash_len = hash_len;
#endif
	}

	LOG_DEBUG("Maximum global storage hash length = %u", max_hash_len);
	free(dict);

	return rc;
}

pss_value_const_t pss_dict_get(const pss_dict_t* dict, const char* key)
{
	if(NULL == dict || NULL == key)
	{
		LOG_ERROR("Invalid arguments");
		return pss_value_const_err();
	}

	_node_t* node = _hash_find((_node_t**)dict->hash, key, 0);

	pss_value_const_t value = {};

	if(NULL != node)
		value = node->value.constval[0];

	return value;
}

int pss_dict_set(pss_dict_t* dict, const char* key, pss_value_t value)
{
	if(NULL == dict || NULL == key || value.kind == PSS_VALUE_KIND_ERROR)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_node_t* node = _hash_find(dict->hash, key, 1);

	if(NULL == node)
		ERROR_RETURN_LOG(int, "Error during inserting new entry to the global table");

	if(ERROR_CODE(int) == pss_value_decref(node->value))
		ERROR_RETURN_LOG(int, "Error during decref the old value");

	node->value = value;

	if(ERROR_CODE(int) == pss_value_incref(node->value))
		ERROR_RETURN_LOG(int, "Error during incref the new value");

	return 0;
}

/**
 * @brief Make a new value
 * @note This function will create a new, empty dictionary anyway
 *       and the value param will be ignored
 * @return The newly created ditionary
 **/
static void* _mkval(void* val)
{
	(void)val;
	return pss_dict_new();
}

/**
 * @brief The type operation that disposes an unseud dict value
 * @param dict_mem The memory the dictionary occupies
 * @return status code
 **/
static int _free(void* dict_mem)
{
	pss_dict_t *dict = (pss_dict_t*)dict_mem;

	if(NULL == dict) ERROR_RETURN_LOG(int, "Invalid arguments");

	return pss_dict_free(dict);
}

/**
 * @brief Convert the dictionary to string
 * @param dict_mem The dictionary memory
 * @param buf The buffer we are using 
 * @param bufsize The buffer of the size
 * @return The string that has been converted
 * @todo Actual implementation for this
 **/
static const char* _tostr(const void* dict_mem, char* buf, size_t bufsize)
{
	(void)dict_mem;
	(void)buf;
	(void)bufsize;

	return "<Fixme: Stringify a dictionary is not supported>";
}

int pss_dict_init()
{
	pss_value_ref_ops_t ops = {
		.mkval = _mkval,
		.free  = _free,
		.tostr = _tostr
	};

	return pss_value_ref_set_type_ops(PSS_VALUE_REF_TYPE_DICT, ops);
}

int pss_dict_finalize()
{
	return 0;
}
