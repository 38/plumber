/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <error.h>

#include <utils/hash/murmurhash3.h>

#include <package_config.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/string.h>
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
	uint32_t  level;    /*!< The level of the hash table */
	uint32_t  nkeys;    /*!< The number of keys */
	uint32_t  keycap;   /*!< The key capacity */
	uint32_t  max_chain;/*!< The maximum chain length */
	const char** keys;  /*!< The key list */
	uint32_t* chain_len;/*!< The chain length for this slot */
	_node_t** hash;     /*!< The global storage hash size */
};

static uint32_t _slot_size[PSS_DICT_SIZE_LEVEL];

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
 * @param level The level of the dictionary
 * @param hash The 128 bit hash code
 * @return The slot id
 **/
static inline uint32_t _hash_slot(uint32_t level, const uint64_t* hash)
{
	uint64_t k = ((1ull << 63) % _slot_size[level]) * 2;
	return (uint32_t)((hash[0] * k + hash[1])%_slot_size[level]);
}

/**
 * @brief Promote the level of the dictionary hash table
 * @param dict The dictionary to promote
 * @return status code
 **/
static inline int _dict_promote(pss_dict_t* dict)
{
	uint32_t level = dict->level + 1; 
	_node_t** table = (_node_t**)calloc(_slot_size[level], sizeof(table[0]));
	uint32_t* chain_len = (uint32_t*)calloc(_slot_size[level], sizeof(chain_len[0]));
	uint32_t max_chain = 0;
	if(NULL == table) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the new hash slot");
	if(NULL == chain_len) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the max chain array");

	uint32_t i;
	for(i = 0; i < _slot_size[dict->level]; i ++)
	{
		_node_t* ptr;
		for(ptr = dict->hash[i]; NULL != ptr;)
		{
			_node_t* this = ptr;
			ptr = ptr->next;

			uint32_t slot = _hash_slot(level, this->hash);
			this->next = table[slot];
			table[slot] = this;
			chain_len[slot] ++;
			if(max_chain < chain_len[slot])
				max_chain = chain_len[slot];
		}
	}

	dict->level = level;
	
	free(dict->hash);
	free(dict->chain_len);

	dict->hash = table;
	dict->chain_len = chain_len;
	dict->max_chain = max_chain;

	LOG_DEBUG("Hash table has been promoted to level %u, new max chain length = %u", dict->level, dict->max_chain);

	return 0;
ERR:
	if(NULL != table) free(table);
	if(NULL != chain_len) free(chain_len);

	return ERROR_CODE(int);
}

/**
 * @brief Find a key from the hash table
 * @param dict The dictionary to insert
 * @param key The key to find
 * @param create Indicates if we want to create a node if it's not exit
 * @return The node we found in the hash table
 **/
static inline _node_t* _hash_find(pss_dict_t* dict, const char* key, int create)
{
	size_t len = strlen(key);
	uint64_t hash[2];

	murmurhash3_128(key, len, 0xf37d543fu, hash);

	uint32_t slot = _hash_slot(dict->level, hash);

	_node_t* ret;
	for(ret = dict->hash[slot]; 
		ret != NULL && 
		(ret->hash[0] != hash[0] || ret->hash[1] != hash[1] || strcmp(ret->key, key) != 0); 
		ret = ret->next);

	if(NULL == ret && create)
	{
		if(NULL == (ret = _node_new(hash, key, len)))
			ERROR_PTR_RETURN_LOG_ERRNO("Cannot create new hash node for global variable %s", key);
		ret->next = dict->hash[slot];
		dict->hash[slot] = ret;
		dict->chain_len[slot] ++;

		if(dict->keycap <= dict->nkeys)
		{
			const char** keylist = (const char**)realloc(dict->keys, sizeof(dict->keys[0]) * dict->keycap * 2);
			if(NULL == keylist) ERROR_PTR_RETURN_LOG_ERRNO("Cannot resize the key list array");
			dict->keys = keylist;
			dict->keycap *= 2;
		}
		
		dict->keys[dict->nkeys ++] = ret->key;

		if(dict->max_chain < dict->chain_len[slot])
			dict->max_chain = dict->chain_len[slot];

		if(dict->max_chain > PSS_DICT_MAX_CHAIN_THRESHOLD && 
		   dict->level < PSS_DICT_SIZE_LEVEL - 1u && 
		   ERROR_CODE(int) == _dict_promote(dict))
			ERROR_PTR_RETURN_LOG("Cannot promote the dictionary");
	}

	return ret;
}

pss_dict_t* pss_dict_new()
{
	pss_dict_t* ret = (pss_dict_t*)calloc(1, sizeof(*ret));
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the global storage");

	ret->level = 0;
	ret->nkeys = 0;
	ret->keycap = 8;

	if(NULL == (ret->keys = (const char**)calloc(ret->keycap, sizeof(ret->keys[0]))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the dictionary key array");

	ret->max_chain = 0;
	
	if(NULL == (ret->chain_len = (uint32_t*)calloc(_slot_size[ret->level], sizeof(ret->chain_len[0]))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the dictionary chain length array");

	if(NULL == (ret->hash = (_node_t**)calloc(_slot_size[ret->level], sizeof(ret->hash[0]))))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the hash table slot");

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->keys) free(ret->keys);
		if(NULL != ret->chain_len) free(ret->chain_len);
		if(NULL != ret->hash) free(ret->hash);

		free(ret);
	}

	return NULL;
}

int pss_dict_free(pss_dict_t* dict)
{
	if(NULL == dict) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;
	uint32_t i;
	for(i = 0; i < _slot_size[dict->level]; i ++)
	{
		_node_t* ptr;
		for(ptr = dict->hash[i]; ptr != NULL; )
		{
			_node_t* this = ptr;
			ptr = ptr->next;
			if(ERROR_CODE(int) == _node_free(this))
				rc = ERROR_CODE(int);
		}
	}

	free(dict->keys);
	free(dict->chain_len);
	free(dict->hash);
	free(dict);

	return rc;
}

pss_value_t pss_dict_get(const pss_dict_t* dict, const char* key)
{
	if(NULL == dict || NULL == key)
	{
		LOG_ERROR("Invalid arguments");
		return pss_value_err();
	}

	_node_t* node = _hash_find((pss_dict_t*)dict, key, 0);

	pss_value_t value = {};

	if(NULL != node)
		value = node->value;

	return value;
}

int pss_dict_set(pss_dict_t* dict, const char* key, pss_value_t value)
{
	if(NULL == dict || NULL == key || value.kind == PSS_VALUE_KIND_ERROR)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	_node_t* node = _hash_find(dict, key, 1);

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
	if(NULL == dict_mem || NULL == buf || bufsize < 1) 
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	const pss_dict_t* dict = (const pss_dict_t*)dict_mem;
	uint32_t nkeys = pss_dict_size(dict);
	if(ERROR_CODE(uint32_t) == nkeys) ERROR_PTR_RETURN_LOG("Cannot get the size of the dictionary");
	const char* ret = buf;

#define _DUMP(fmt, args...) do {\
		int rc = snprintf(buf, bufsize, fmt, ##args); \
		if(rc < 0) ERROR_PTR_RETURN_LOG("Cannot dump bytes to string buffer");\
		if((size_t)rc > bufsize - 1) rc = (int)(bufsize - 1);\
		bufsize -= (size_t)rc;\
		buf += (size_t)rc; \
	} while(0);

	_DUMP("{ ");

	uint32_t i;
	for(i = 0; i < nkeys; i ++)
	{
		const char* key = pss_dict_get_key(dict, i);
		if(NULL == key) ERROR_PTR_RETURN_LOG("Cannot get the key at index %u", i);
		pss_value_t value = pss_dict_get(dict, key);
		if(PSS_VALUE_KIND_ERROR == value.kind) ERROR_PTR_RETURN_LOG("Cannot get the value for key %s", key);

		char* result = pss_string_literal(key, buf, bufsize);

		if(NULL == result) ERROR_PTR_RETURN_LOG("Cannot convert the key to string literal");

		if(result[0] == 0) break;   /* We just used up the buffer */

		size_t len = strlen(result);
		buf += len;
		bufsize -= len;

		_DUMP(": ");

		size_t written;

		if(value.kind == PSS_VALUE_KIND_REF && PSS_VALUE_REF_TYPE_STRING == pss_value_ref_type(value))
		{
			char* result = pss_string_literal(key, buf, bufsize);
			if(NULL == result) ERROR_PTR_RETURN_LOG("Cannot convert the value to string literal");
			written = strlen(result);
		}
		else
		{
			size_t written = pss_value_strify_to_buf(value, buf, bufsize);
			if(ERROR_CODE(size_t) == written) ERROR_PTR_RETURN_LOG("Cannot dump the value to the string");
		}

		buf += written;
		bufsize -= written;

		if(i != nkeys - 1) _DUMP(", ");
	}
	
	_DUMP(" }");
#undef _DUMP
	return ret; 
}

int pss_dict_init()
{
	_slot_size[0] = PSS_DICT_INIT_SIZE;

	uint32_t i, j;
	for(i = 1; i < sizeof(_slot_size) / sizeof(_slot_size[0]); i ++)
		for(_slot_size[i] = 2 * _slot_size[i - 1] + 1; ; _slot_size[i] += 2)
		{
			for(j = 2; j * j <= _slot_size[i]; j ++)
				if(_slot_size[i] % j == 0) break;
			if(j * j > _slot_size[i])
			{
				LOG_DEBUG("The dictionary slot size at level %u has been set to %u", i, _slot_size[i]);
				break;
			}
		}

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


uint32_t pss_dict_size(const pss_dict_t* dict)
{
	if(NULL == dict) ERROR_RETURN_LOG(uint32_t, "Invalid arguments");

	return dict->nkeys;
}

const char* pss_dict_get_key(const pss_dict_t* dict, uint32_t i)
{
	if(NULL == dict) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(i >= dict->nkeys) ERROR_PTR_RETURN_LOG("Index out of range");

	return dict->keys[i];
}
