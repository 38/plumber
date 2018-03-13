/**
 * Copyright (C), Hao Hou
 **/
/**
 * @brief The Binary Trie implementation
 * @details A binary trie is a trie that only splits into two subtree.
 *          This desgin might be very uneffecient, but we introduce a path compression
 *          mechanism. For each node we are able to match multiple bits instead of one
 *          if the multiple bits can split the set of string into two parts.
 * @file servlet/network/http/include/trie.h
 **/

#ifndef __TRIE_H__
#define __TRIE_H__

/**
 * @brief The trie data structure
 **/
typedef struct _trie_t trie_t;

/**
 * @brief key-value pair 
 **/
typedef struct {
	char const* key;
	void const* val;
} trie_kv_pair_t;

/**
 * @brief The search state for a trie search
 **/
typedef struct {
	uint32_t            code;
	uint32_t            matched_len;
} trie_search_state_t;

/**
 * @brief Create a state that can be used as the initial state
 * @return The state new created
 **/
static inline void trie_state_init(trie_search_state_t* state)
{
	state->code = 0;
	state->matched_len = 0;
}

/**
 * @brief Create a trie index maps keys to values
 * @param data   All the data we want to index
 * @param count  The number of items
 * @return newly created trie
 **/
trie_t* trie_new(trie_kv_pair_t* data, size_t count);

/**
 * @brief Dipose a trie object
 * @param trie The trie object to dispose
 * @return status code
 **/
int trie_free(trie_t* trie);

/**
 * @brief Search the LCM using the trie
 * @param trie The trie object
 * @param state The state variable tracking the search state
 * @param key The next chunk of key we feeded in
 * @param key_size The size of the key
 * @param result The data we have found
 * @return The number of bytes consumed by the trie seach process. The return value is 0 is a good indicator
 *         of match has finished. error code will be returned on all error cases
 **/
size_t trie_search(const trie_t* trie, trie_search_state_t* state, const char* key, size_t key_size, void const** result);

#endif /*__TRIE_H__*/
