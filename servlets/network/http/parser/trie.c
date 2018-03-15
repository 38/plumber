/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <trie.h>

/**
 * @brief The internal data structure for a trie node
 **/
typedef struct _trie_node_t {
	uint8_t  mask[2];                    /*!< The mask code for the boder of the chunk */
	uint16_t length;                     /*!< The length of the chunk in number of bytes (at most 8192 bytes) */
	uint32_t has_left:1;                 /*!< If this node has a left children */
	uint32_t has_right:1;                /*!< If this node has a right children */
	uint32_t right_offset:30;            /*!< The second children's offset from the current node */
} _trie_node_t;

/**
 * @brief The actual data structure for the trie
 * @note  Use the storeage structure similar to binary heap, but we also memorize the child offest as well
 **/
struct _trie_t {
	_trie_node_t*   node_array;   /*!< The node array */
	const void**    val_data;     /*!< The value data for each node */
	const char**    key_data;     /*!< The key data array */
};

static int _key_cmp(const void* pa, const void* pb)
{
	const trie_kv_pair_t* a = (const trie_kv_pair_t*)pa;
	const trie_kv_pair_t* b = (const trie_kv_pair_t*)pb;

	return strcmp(a->key, b->key);
}

static inline uint32_t _compute_trie_size(const trie_kv_pair_t* data, uint32_t offset, size_t size)
{
	if(size < 2) return (uint32_t)size;

	uint16_t length;
	uint32_t done = 0, split = 0, is_word = 0;;

	for(length = 0; length < 0xffffu && !done; length ++)
	{
		uint32_t i;
		int state = -1;
		for(i = 0; i < size && !done; i ++)
		{
			uint32_t byte_offset = (offset + length) / 8;
			uint32_t bit_offset  = (offset + length) % 8;

			if(data->key[byte_offset] == 0) 
			{
				is_word = 1;
				continue;
			}

			int cur_bit = ((uint8_t)data[i].key[byte_offset] & (0x80u >> bit_offset)) != 0;

			if(state == -1) state = cur_bit;

			if(state != cur_bit) 
			{
				done = 1;
				split = i;
			}
		}
	}

	if(done)
		return _compute_trie_size(data + is_word, offset + length, split - is_word) + _compute_trie_size(data + split, offset + length, size - split) + 1;
	else
		return _compute_trie_size(data + is_word, offset + length, size - is_word) + 1;
}

static inline void _init_node(trie_t* trie, uint32_t slot, char const* key, uint32_t key_offset, uint32_t key_length, void const* value)
{
	uint32_t key_byte_ofs = key_offset / 8;
	uint32_t key_bit_ofs  = key_offset % 8;
	uint32_t key_bit_end  = (key_bit_ofs + key_length) % 8;

	if(key_bit_end == 0) key_bit_end = 8;

	trie->key_data[slot] = key + key_byte_ofs;
	trie->val_data[slot] = value;

	if(key_offset / 8 != (key_offset + key_length - 1) / 8)
	{
		/* If this key has covers more than one byte */
		trie->node_array[slot].mask[0] = (uint8_t)(0xffu >> key_bit_ofs);
		trie->node_array[slot].mask[1] = (uint8_t)((0xffu << (8 - key_bit_end)) & 0xff);
		trie->node_array[slot].length = (uint16_t)(1 + (key_length - (8 - key_bit_ofs) + 7) / 8);
	}
	else
	{
		/* If it's in one byte */
		uint32_t mask = (0xffu >> key_bit_ofs) & ((0xffu << (8 - key_bit_end)) & 0xff);
		uint32_t last_bit = (((mask - 1) ^ mask) + 1) / 2;
		mask ^= last_bit;
		trie->node_array[slot].mask[0] = mask & 0xffu;
		trie->node_array[slot].mask[1] = last_bit & 0xffu;
		trie->node_array[slot].length = 1;
	}

	trie->node_array[slot].has_left = 0;
	trie->node_array[slot].has_right = 0;
	trie->node_array[slot].right_offset = 0;
}

/**
 * @brief Build the trie
 * @param data   The the key value pair that we need to build the index
 * @param offset The number of bits that has been already indexed
 * @paarm size The number of items in the data array
 * @param buf The buffer that is used for the root node
 * @param buf The buffer that is used for the children
 * @return number of nodes has been created
 **/
static inline uint32_t _build_trie(const trie_kv_pair_t* data, uint32_t offset, uint32_t size, trie_t* trie, uint32_t slot)
{
	if(size == 0)
	{
		return 0;
	}

	if(size == 1)
	{
		_init_node(trie, slot, data[0].key, offset, (uint32_t)(strlen(data[0].key) * 8 - offset), data[0].val);
		return 1;
	}
	
	uint16_t length;
	uint32_t done = 0, split = 0, is_word = 0;
	

	for(length = 0; length < 0xffffu && !done; length ++)
	{
		uint32_t i;
		int state = -1;
		for(i = 0; i < size && !done; i ++)
		{
			uint32_t byte_offset = (offset + length) / 8;
			uint32_t bit_offset  = (offset + length) % 8;

			if(data->key[byte_offset] == 0) 
			{
				is_word = 1;
				continue;
			}

			int cur_bit = ((uint8_t)data[i].key[byte_offset] & (0x80u >> bit_offset)) != 0;

			if(state == -1) state = cur_bit;

			if(state != cur_bit) 
			{
				done = 1;
				split = i;
			}
		}
	}

	_init_node(trie, slot, data[0].key, offset, length, is_word ? data[0].val : NULL);

	if(done)
	{
		trie->node_array[slot].has_left = 1;
		trie->node_array[slot].has_right = 1;

		uint32_t left_size = _build_trie(data + is_word, offset + length, split - is_word, trie, slot + 1);
		uint32_t right_size = _build_trie(data + split, offset + length, size - split, trie, slot + left_size + 1);
		trie->node_array[slot].right_offset = ((uint32_t)left_size & 0x3ffffffful);
		return left_size + right_size + 1;
	}
	else
	{
		int last_bit = (data[offset + length - 1].key[offset / 8] & (0x80 >> ((offset + length) % 8)));

		if(0 == last_bit)
			trie->node_array[slot].has_left = 1;
		else
			trie->node_array[slot].has_right = 1;

		return _build_trie(data + is_word, offset + length, size - is_word, trie, slot + 1) + 1;
	}

}

trie_t* trie_new(trie_kv_pair_t* data, size_t count)
{
	if(NULL == data)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	qsort(data, count, sizeof(trie_kv_pair_t), _key_cmp);

	size_t i;
	for(i = 0; count > 0 && i < count - 1; i ++)
		if(strcmp(data[i].key, data[i + 1].key) == 0)
			ERROR_PTR_RETURN_LOG("Invalid arguments: Duplicated key found %s", data[i].key);

	size_t trie_size = _compute_trie_size(data, 0, count);

	trie_t* ret = (trie_t*)calloc(sizeof(trie_t), 1);
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the trie");

	if(trie_size > 0)
	{

		if(NULL == (ret->node_array = (_trie_node_t*)calloc(sizeof(_trie_node_t), trie_size)))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the node array");
		if(NULL == (ret->key_data = (char const**)calloc(sizeof(char const*), trie_size)))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocat ememory for the key data");
		if(NULL == (ret->val_data = (void const**)calloc(sizeof(void const*), trie_size)))
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the val data");

		(void)_build_trie(data, 0, (uint32_t)count, ret, 0);
	}

	return ret;
ERR:
	if(NULL != ret->node_array) free(ret->node_array);
	if(NULL != ret->key_data) free(ret->key_data);
	if(NULL != ret->val_data) free(ret->val_data);
	free(ret);

	return NULL;
}

int trie_free(trie_t* trie)
{
	if(NULL == trie)
		ERROR_RETURN_LOG(int, "Invalid arguments");
	
	if(NULL != trie->node_array) free(trie->node_array);
	if(NULL != trie->key_data) free(trie->key_data);
	if(NULL != trie->val_data) free(trie->val_data);
	free(trie);

	return 0;
}

static inline int _mem_match(const char* a, const char* b, size_t sz)
{
	return 0 == memcmp(a, b, sz);
}

size_t trie_search(const trie_t* trie, trie_search_state_t* state, const char* key, size_t key_size, void const** result)
{
	if(NULL == trie || NULL == state || NULL == key || NULL == result || key_size == 0)
		ERROR_RETURN_LOG(size_t, "Invalid arguments");

	if(trie->node_array == NULL) 
	{
		*result = NULL;
		state->code = ERROR_CODE(uint32_t);
		return 0;
	}

	size_t ret = 0;
	uint32_t matched_len = state->matched_len;
	const _trie_node_t* cur_node = trie->node_array + state->code;
	const char* cur_key = trie->key_data[state->code];
	const void* cur_val = trie->val_data[state->code];

	for(;key_size > 0;)
	{
		if(cur_node->length == 1)
		{
			/* If the match segment is smaller than 1 byte, thus we just compre the current byte, in this case the matched_pos must be 0 */
			uint8_t ch = ((uint8_t)key[0] ^ (uint8_t)cur_key[0]);

			if(!(cur_node->mask[0] & ch))
			{
				/* Check if we have matched the full byte */
				if((cur_node->mask[1] & 1) == 1)
				{
					ret ++;
					key ++;
					key_size --;
					matched_len ++;
				}

				if(!(cur_node->mask[1] & ch))
					goto LEFT;
				else
					goto RIGHT;
			}
			else goto MATCH_FAILED;
		}
		else
		{
			/*If the match segment is multiple bytes */
			if(matched_len == 0)
			{
				uint8_t ch = ((uint8_t)key[0] ^ (uint8_t)cur_key[0]);
				/* We need to match the first byte */
				if((ch & cur_node->mask[0]) == 0)
					matched_len = 1;
				else goto MATCH_FAILED;

				key ++;
				key_size --;
				ret ++;
			}

			if(key_size > 0 && cur_node->length > matched_len + 1)
			{
				/* We need to match the completed chunks of bytes */
				uint32_t bytes_to_comp = (uint32_t)(cur_node->length - 1u - matched_len);
				if(bytes_to_comp > key_size)
					bytes_to_comp = (uint32_t)key_size;

				if(!_mem_match(key, cur_key + matched_len, bytes_to_comp))
					goto MATCH_FAILED;

				matched_len += bytes_to_comp;
				key += bytes_to_comp;
				key_size -= bytes_to_comp;
				ret += (size_t)bytes_to_comp;
			}

			if(key_size > 0 && matched_len + 1 == cur_node->length)
			{
				/* If we have the last byte to compare */
				uint8_t prefix_mask = cur_node->mask[1];
				prefix_mask &= (uint8_t)~((prefix_mask - 1u) ^ prefix_mask);
				uint8_t suffix_mask = cur_node->mask[1] ^ prefix_mask;

				uint8_t ch = (uint8_t)cur_key[matched_len] ^ (uint8_t)key[0];

				if(suffix_mask == 1)
				{
					ret ++;
					key_size --;
					key ++;
					matched_len ++;
				}

				if(0 == (ch & prefix_mask))
				{
					/* If we can match the prefix */
					if(ch & suffix_mask)
						goto RIGHT;
					else
						goto LEFT;
				}
				else goto MATCH_FAILED;
			}
		}
		/* If we get here, it means we need another iteration */
		continue;
LEFT:
		/* We need to goto the left child */
		if(cur_val != NULL) goto MATCH_SUCCESS;
		if(!cur_node->has_left) goto MATCH_FAILED;
		cur_node = cur_node + 1;
		cur_key = trie->key_data[(uint32_t)(cur_node - trie->node_array)];
		cur_val = trie->val_data[(uint32_t)(cur_node - trie->node_array)];
		matched_len = 0;
		continue;
RIGHT:
		if(cur_val != NULL) goto MATCH_SUCCESS;
		if(!cur_node->has_right) goto MATCH_FAILED;
		cur_node = cur_node + cur_node->right_offset + 1; 
		cur_key = trie->key_data[(uint32_t)(cur_node - trie->node_array)];
		cur_val = trie->val_data[(uint32_t)(cur_node - trie->node_array)];
		matched_len = 0;
	}

	/* If we used up the buffer, save the state and exit */
	state->code = (uint32_t)(cur_node - trie->node_array);
	state->matched_len = matched_len;

	return ret;

MATCH_FAILED:

	*result = NULL;
	state->code = ERROR_CODE(uint32_t);
	return 0;

MATCH_SUCCESS:
	state->code = ERROR_CODE(uint32_t);
	*result = cur_val;
	return ret;
}
