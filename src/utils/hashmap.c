/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include <error.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/static_assertion.h>
#include <utils/mempool/oneway.h>

/**
 * @brief a node in the hash table
 **/
typedef struct _node_t {
	uint64_t hashcode;   /*!< the hash code for this node */
	size_t   key_size;   /*!< the size of the key */
	size_t   val_size;   /*!< the size of the value */
	struct _node_t* next;/*!< the linked list pointer in the slot */
	uintpad_t __padding__[0];
	char     data[0];    /*!< the actual data section */
} _node_t;
STATIC_ASSERTION_LAST(_node_t, data);
STATIC_ASSERTION_SIZE(_node_t, data, 0);

/**
 * @brief the actual data structure for as hash map
 **/
struct _hashmap_t {
	size_t    num_slots; /*!< the number of slots */
	mempool_oneway_t* pool; /*!< the memory pool */
	uintpad_t __padding__[0];
	_node_t*  slots[0]; /*!< the number of slots */
};
STATIC_ASSERTION_LAST(hashmap_t, slots);
STATIC_ASSERTION_SIZE(hashmap_t, slots, 0);

/**
 * @brief get the hash code from the data section
 * @param data the pointer to the data to hash
 * @param count the number of bytes to hash
 * @note this is the MurmurHash2
 * @return the result hash code
 **/
static inline uint64_t _hash(const void* data, size_t count)
{
	const uint64_t m = 0xc6a4a7935bd1e995ull;
	const int r = 47;

	uint64_t ret = 0;

	const uint8_t*   u8  = (const uint8_t*)data;
	const uint64_t* u64 = (const uint64_t*)data;
	u8 += count - (count % sizeof(uint64_t));

	for(;count > sizeof(uint64_t); count -= sizeof(uint64_t))
	{
		uint64_t cur = *(u64++);

		cur *= m;
		cur ^= cur >> r;
		cur *= m;

		ret ^= cur;
		ret *= m;
	}
	switch(count)
	{
#define _ADD_BIT(n) case n: ret^= ((uint64_t)u8[(n)-1]) << (((n) - 1) * 8);
		_ADD_BIT(7);
		_ADD_BIT(6);
		_ADD_BIT(5);
		_ADD_BIT(4);
		_ADD_BIT(3);
		_ADD_BIT(2);
		_ADD_BIT(1);
#undef _ADD_BIT
		ret *= m;
	}

	ret ^= ret >> r;
	ret *= m;
	ret ^= ret >> r;

	return ret;
}

#define _KEY(node) ((node)->data)
#define _VAL(node) ((node)->data + (node)->key_size)

static inline _node_t* _node_alloc(hashmap_t* hm, const void* key, size_t key_size, const void* val, size_t val_size)
{
	_node_t* ret = (_node_t*)mempool_oneway_alloc(hm->pool, sizeof(_node_t) + key_size + val_size);

	if(NULL == ret)
	{
		LOG_ERROR_ERRNO("cannot allocate memory for hash node");
		return NULL;
	}

	ret->hashcode = _hash(key, key_size);

	ret->key_size = key_size;
	ret->val_size = val_size;

	memcpy(_KEY(ret), key, key_size);
	if(val_size > 0) memcpy(_VAL(ret), val, val_size);

	return ret;
}

static inline int _node_key_equal(const void* key, size_t key_size, const _node_t* node)
{
	if(key_size != node->key_size) return 0;

	size_t u64_cnt = key_size / sizeof(uint64_t);
	const uint64_t* u64_a = (const uint64_t*)key;
	const uint64_t* u64_b = (const uint64_t*)_KEY(node);

	for(;u64_cnt > 0 && (*u64_a == *u64_b); u64_a ++, u64_b ++, key_size -= sizeof(uint64_t), u64_cnt--);
	if(u64_cnt > 0) return 0;
	if(key_size == 0) return 1;

#if __BYTE_ORDER__ ==  __ORDER_LITTLE_ENDIAN__   /* TODO: this assume we are compling with GCC */
	return ((u64_a[0] ^ u64_b[0]) & (((uint64_t)-1) >> (64 - key_size * 8))) == 0;
#else
	return ((u64_a[0] ^ u64_b[0]) & (((uint64_t)-1) << (64 - key_size * 8))) == 0;
#endif
}

static inline const _node_t* _hash_find(const hashmap_t* hashtable, const void* key, size_t key_size)
{
	uint64_t hashcode = _hash(key, key_size);

	const _node_t* ptr = hashtable->slots[hashcode % hashtable->num_slots];

	for(;NULL != ptr && !_node_key_equal(key, key_size, ptr); ptr = ptr->next);

	return ptr;
}

hashmap_t* hashmap_new(size_t num_slots, size_t init_pool)
{
	(void) init_pool;
	hashmap_t* ret = (hashmap_t*)calloc(1, sizeof(hashmap_t) + sizeof(_node_t*) * num_slots);

	if(NULL == ret)
	{
		LOG_ERROR_ERRNO("cannot allocate memory for hashmap");
		return NULL;
	}

	if(NULL == (ret->pool = mempool_oneway_new(init_pool)))
	{
		LOG_ERROR("cannot create memory pool");
		free(ret);
		return NULL;
	}

	ret->num_slots = num_slots;

	return ret;
}

int hashmap_free(hashmap_t* hashmap)
{
	if(NULL == hashmap)
	{
		LOG_ERROR("Invalid arguments");
		return ERROR_CODE(int);
	}

	if(hashmap->pool != NULL) mempool_oneway_free(hashmap->pool);
	free(hashmap);
	return 0;
}

static inline void _fill_result(hashmap_find_res_t* result, const _node_t* node)
{
	result->key_size = node->key_size;
	result->val_size = node->val_size;
	result->key_data = _KEY(node);
	result->val_data = _VAL(node);
}

int hashmap_insert(hashmap_t* hashmap, const void* key_data, size_t key_size,
                   const void* val_data, size_t val_size,
                   hashmap_find_res_t* result, int override)
{
	if(NULL == hashmap || NULL == key_data || 0 == key_size)
	{
		LOG_ERROR("invalid arguments");
		return ERROR_CODE(int);
	}

	const _node_t* rc;

	if((rc = _hash_find(hashmap, key_data, key_size)) != NULL)
	{
		if(!override)
		{
			_fill_result(result, rc);
			return 0;
		}
		else if(rc->val_size == val_size)
		{
			if(val_size > 0)
			    memcpy(_VAL((_node_t*)rc), val_data, val_size);
			_fill_result(result, rc);
			return 1;
		}
		else
		    ERROR_RETURN_LOG(int, "attempt put value data in different size to the same item");
	}

	_node_t* node = _node_alloc(hashmap, key_data, key_size, val_data, val_size);
	if(NULL == node)
	{
		LOG_ERROR("cannot allocate hash node");
		return ERROR_CODE(int);
	}

	uint64_t slot_idx = node->hashcode % hashmap->num_slots;

	node->next = hashmap->slots[slot_idx];
	hashmap->slots[slot_idx] = node;

	if(NULL != result)
	    _fill_result(result, node);
	return 1;
}

int hashmap_find(const hashmap_t* hashmap, const void* key_data, size_t key_size, hashmap_find_res_t* result)
{
	if(NULL == hashmap || NULL == key_data || 0 == key_size || NULL == result)
	{
		LOG_ERROR("Invalid arguments");
		return ERROR_CODE(int);
	}

	const _node_t* node = _hash_find(hashmap, key_data, key_size);

	if(NULL == node) return 0;

	_fill_result(result, node);

	return 1;
}
