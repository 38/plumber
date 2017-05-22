/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <sys/stat.h>

#include <error.h>

#include <pservlet.h>

#include <fcache.h>
#include <onexit.h>
#include <mempool.h>

/**
 * @brief the size of the cache hash table
 * @todo this should be configurable. Currently, we just use the
 *       simple replacement policy: If there's a conflict, then we
 *       replace it. <br/>
 *       Also, the cache is thread local, so that we can avoid the
 *       locks and mutex. But it's still not clear what actually comes out
 *       by this. <br/>
 *       Also, it seems we don't have a way to configure pstd, which is annoying
 **/
#define _CACHE_HASH_SIZE 32771

/**
 * @brief The maximum time to live for a cache slot, all the cache that elder than
 *        this time limit (in seconds) should be considered invalid
 * @todo  also this should be configurable
 **/
#define _CACHE_TTL 300

/**
 * @brief the maximum number of bytes allowed for a single file
 * @todo  make this configurable
 **/
#define _MAX_FILE_SIZE (1u<<20)     /* 1M for each file */

/**
 * @brief The maximum data size of the cache for a single thread
 **/
#define _MAX_CACHE_SIZE (32u<<20)   /* 32M for each thread */

/**
 * @brief the data structure  for a cache entry
 **/
typedef struct {
	uint32_t  valid:1;     /*!< if this this a valid cache */
	time_t    timestamp;   /*!< the timestamp when we load the entry */
	uint32_t  idx;         /*!< this is just the index of the slot in the slot array, because we should avoid access the thread local multiple times */
	uint32_t  refcnt;      /*!< how many references do we currently have */
	size_t    size;        /*!< the number of bytes that has been loaded to cache */
	uint64_t  hash[2];     /*!< the 128 bit hash code for the filename */
	uint32_t  lru_prev;    /*!< the previous element in the LRU linked list */
	uint32_t  lru_next;    /*!< the next element in the LRU linked list */
#ifdef PSTD_FILE_CACHE_STRICT_KEY_COMP
	char*     filename;    /*!< the filename of the entry */
#endif
	struct stat stat; /*!< the cached stat */
	int8_t*   data;   /*!< the data pages for this cache */
} _cache_entry_t;

/**
 * @brief the first element, which means the latest accessed entry
 **/
static __thread uint32_t _lru_first;

/**
 * @brief the last element, which means the element we don't access for the longest time
 **/
static __thread uint32_t _lru_last;

/**
 * @brief the thread cache table we used for each thread
 **/
static __thread _cache_entry_t* _cache_table = NULL;

/**
 * @brief the hash seed we should use
 **/
static __thread uint32_t _hash_seed;

/**
 * @brief the total data size for current thread's cache
 **/
static __thread size_t _data_size = 0;

/**
 * @brief The actual data structure for a reference to the file cache entry
 * @details This is the universal descriptor for both file in the cache and file that reads directly
 *         when the caching system is not able to cache the file, it will return a uncached file refernece
 *         In this way, the upper layer application do not need to care about if the file is read from
 *         the cache
 **/
struct _pstd_fcache_file_t {
	int                           cached;   /*!< if this file reference pointed to a cache entry */
	union{
		const _cache_entry_t*     cache;    /*!< the entry to the cache */
		FILE*                     file;     /*!< if we need to use the file directly, for example, large file or cache conflict */
	};
	size_t                        offset;   /*!< the current offset from the head of the stream */
	size_t                        size;     /*!< the total size of the file */
};

/**
 * @brief get the size of the thread local cache
 * @note  this is currently just return the macro, but later we should make it read the configuration
 * return the size in number of elements
 **/
static inline uint32_t _cache_hash_size()
{
	return _CACHE_HASH_SIZE;
}

/**
 * @brief get the max time to live for each cache entry
 * @note  this should be configurable, make this read from a config file
 * @return the TTL in seconds
 **/
static inline uint32_t _cache_ttl()
{
	return _CACHE_TTL;
}

/**
 * @brief get the file cache size limit, which means if the file is larger than the size,
 *        we do not cache it
 * @note  this should be configurable
 * @return the size limit in number of bytes
 **/
static inline uint32_t _max_file_size()
{
	return _MAX_FILE_SIZE;
}

/**
 * @brief get the max size of the cache for a single thread
 * @note  this should be configurable
 * @return the max size of the thread cache
 **/
static inline size_t _max_cache_size()
{
	return _MAX_CACHE_SIZE;
}

/**
 * @brief the callback function that is used to cleanup all the cache entry and the cache table itself
 * @param cache_table the cache_table to dispose
 * @return nothing
 **/
static void _clean_cache(void* cache_table)
{
	if(NULL == cache_table) return;
	_cache_entry_t* table = (_cache_entry_t*)cache_table;

	uint32_t i;

	for(i = 0; i < _cache_hash_size(); i ++)
	{
		_cache_entry_t* entry = table + i;
		if(0 == entry->valid) continue;

		if(entry->data != NULL) free(entry->data);

#ifdef PSTD_FILE_CACHE_STRICT_KEY_COMP
		if(NULL != entry->filename)
		    free(entry->filename);
#endif
	}

	free(table);
}

/**
 * @brief ensure the thread local cache is initialized
 * @return status code
 **/
static inline int _ensure_thread_init()
{
	if(_cache_table == NULL)
	{
		LOG_DEBUG("The thread local cache is not initialized yet, now doing the initialization");
		if(NULL == (_cache_table = (_cache_entry_t*)malloc(sizeof(_cache_table[0]) * _cache_hash_size())))
		    ERROR_RETURN_LOG_ERRNO(int, "Can not allocate memory for the thread local cache");

		uint32_t i;
		for(i = 0; i < _cache_hash_size(); i ++)
		    _cache_table[i].valid = 0, _cache_table[i].idx = i;

		if(ERROR_CODE(int) == pstd_onexit(_clean_cache, _cache_table))
		{
			free(_cache_table);
			ERROR_RETURN_LOG(int, "Cannot register the cleanup function for the cache table");
		}

		/* Genereate a random seed, so that the external client won't know how to make collision in our hash table */
		struct timespec ts;
		if(clock_gettime(CLOCK_REALTIME, &ts) < 0)
		{
			LOG_WARNING("Cannot get the high resolution timestamp, use low resolution one instead");
			ts.tv_nsec = (long)time(NULL);
		}
		srand((unsigned)ts.tv_nsec);
		/* We need to do this, because the RAND_MAX is not guarenteed fill all the 32 bits up */
		uint64_t upper_bound = 1;
		while(upper_bound <= 0xffffffffu)
		{
			_hash_seed = (uint32_t)rand() + _hash_seed * RAND_MAX;
			upper_bound *= RAND_MAX;
		}

		LOG_DEBUG("The hash seed is %u", _hash_seed);

		_lru_first = _lru_last = ERROR_CODE(uint32_t);

		LOG_DEBUG("The thread local cache is sucessfully initailized");
	}

	return 0;
}

static inline uint64_t _murmurhash3_rotl64(uint64_t x, int8_t r)
{
	return (x << r) | (x >> (64 - r));
}

static inline uint64_t _murmurhash3_fmix64(uint64_t k)
{
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdull;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53ull;
	k ^= k >> 33;

	return k;
}

/**
 * @brief currently we use the murmur hash 3 as the hash function
 * @note  see the origin implementation from http://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp
 * @param key the key to hash
 * @param len the length of the key
 * @param out the output array
 * @return nothing
 **/
static inline void _murmurhash3_128(const void* key, const size_t len, uint64_t out[2])
{
	size_t tail_size = len % 16, i;
	const uint8_t *tail = ((const uint8_t*)key) + (len - tail_size);
	const uint64_t *head = (const uint64_t*)key;

	out[0] = out[1] = _hash_seed;

	const uint64_t c1 = 0x87c37b91114253d5ull;
	const uint64_t c2 = 0x4cf5ad432745937full;

	for(i = 0; i < len / 16; i ++)
	{
		uint64_t k1 = head[i * 2];
		uint64_t k2 = head[i * 2 + 1];

		k1 *= c1;
		k1 = _murmurhash3_rotl64(k1, 31);
		k1 *= c2;
		out[0] ^= k1;
		out[0] = _murmurhash3_rotl64(out[0], 27);
		out[0] += out[1];
		out[0] = out[0] * 5 + 0x52dce729;

		k2 *= c2;
		k2 = _murmurhash3_rotl64(k2, 33);
		k2 *= c1;
		out[1] ^= k2;
		out[1] = _murmurhash3_rotl64(out[1], 31);
		out[1] += out[0];
		out[1] = out[1] * 5 + 0x38495ab5;
	}

	uint64_t k1 = 0;
	uint64_t k2 = 0;

	switch(tail_size)
	{
		case 15: k2 ^= ((uint64_t)tail[14]) << 48;
		case 14: k2 ^= ((uint64_t)tail[13]) << 40;
		case 13: k2 ^= ((uint64_t)tail[12]) << 32;
		case 12: k2 ^= ((uint64_t)tail[11]) << 24;
		case 11: k2 ^= ((uint64_t)tail[10]) << 16;
		case 10: k2 ^= ((uint64_t)tail[9]) << 8;
		case 9: k2 ^= ((uint64_t)tail[8]) << 0;
		    k2 *= c2;
		    k2 = _murmurhash3_rotl64(k2, 33);
		    k2 *= c1;
		    out[1] ^= k2;
		case 8: k1 ^= ((uint64_t)tail[7]) << 56;
		case 7: k1 ^= ((uint64_t)tail[6]) << 48;
		case 6: k1 ^= ((uint64_t)tail[5]) << 40;
		case 5: k1 ^= ((uint64_t)tail[4]) << 32;
		case 4: k1 ^= ((uint64_t)tail[3]) << 24;
		case 3: k1 ^= ((uint64_t)tail[2]) << 16;
		case 2: k1 ^= ((uint64_t)tail[1]) << 8;
		case 1: k1 ^= ((uint64_t)tail[0]) << 0;
		    k1 *= c1;
		    k1 = _murmurhash3_rotl64(k1, 31);
		    k1 *= c2;
		    out[0] ^= k1;
	}

	out[0] ^= len;
	out[1] ^= len;

	out[0] += out[1];
	out[1] += out[0];

	out[0] = _murmurhash3_fmix64(out[0]);
	out[1] = _murmurhash3_fmix64(out[1]);

	out[0] += out[1];
	out[1] += out[0];
}

/**
 * @brief check if the cache entry is the one we are looking for
 * @param entry the cache entry
 * @param expected_hash the hash code we are expecting
 * @param filename the actual file name we are looking for
 * @return the check result, 1 means found, 0 means not found, -1 means expired
 **/
static inline int _entry_matches(const _cache_entry_t* entry, const uint64_t* expected_hash, const char* filename)
{
#ifndef PSTD_FILE_CACHE_STRICT_KEY_COMP
	(void)filename;
#endif
	if(entry->valid == 0) return 0;

	if(entry->hash[0] == expected_hash[0] &&
	   entry->hash[1] == expected_hash[1])
	{
#ifdef PSTD_FILE_CACHE_STRICT_KEY_COMP
		if(strcmp(filename, entry->filename) != 0)
		    return 0;
#endif
		time_t ts = time(NULL);
		return ((ts - entry->timestamp) > _cache_ttl()) ? -1 : 1;
	}

	return 0;
}

/**
 * @brief add a new entry to the LRU list
 * @param idx the slot index to add
 * @note this function assume the entry is not prevoiusly in the LRU list, which
 *       means it should be invalid prevously
 * @return nothing
 **/
static inline void _lru_add(uint32_t idx)
{
	_cache_entry_t* begin = _cache_table;
	begin[idx].lru_prev = ERROR_CODE(uint32_t);
	begin[idx].lru_next = _lru_first;

	if(_lru_first != ERROR_CODE(uint32_t))
	    begin[_lru_first].lru_prev = idx;
	else
	    _lru_last = idx;

	_lru_first = idx;
}

/**
 * @brief remove an existing entry from the LRU list
 * @param idx the slot index to remove
 * @return nothing
 **/
static inline void _lru_remove(uint32_t idx)
{
	_cache_entry_t* begin = _cache_table;

	if(begin[idx].lru_prev != ERROR_CODE(uint32_t))
	    begin[begin[idx].lru_prev].lru_next = begin[idx].lru_next;
	else
	    _lru_first = begin[idx].lru_next;

	if(begin[idx].lru_next != ERROR_CODE(uint32_t))
	    begin[begin[idx].lru_next].lru_prev = begin[idx].lru_prev;
	else
	    _lru_last = begin[idx].lru_prev;

}

/**
 * @brief touch the lru entry, which means move the given entry to the first in LRU list
 * @param idx the entry to touch
 * @return nothing
 **/
static inline void _lru_touch(uint32_t idx)
{
	_lru_remove(idx);
	_lru_add(idx);
}

/**
 * @brief create a cached file reference, which means we are refering something in the cache
 * @param entry the entry we want to create the reference for
 * @return the file object has been created
 **/
static inline pstd_fcache_file_t* _create_cached_file(_cache_entry_t* entry)
{
	pstd_fcache_file_t* ret = (pstd_fcache_file_t*)pstd_mempool_alloc(sizeof(*ret));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the cached file");

	ret->cached = 1;
	ret->cache = entry;
	ret->offset = 0;

	uint32_t new_val;

	do{
		new_val = entry->refcnt + 1;
	} while(!__sync_bool_compare_and_swap(&entry->refcnt, new_val - 1, new_val));

	ret->size = entry->size;
	_lru_touch(entry->idx);

	LOG_DEBUG("Load file from cache");

	return ret;
}

/**
 * @brief create a uncached file, which will read the content from the disk directly
 * @param fp the file pointer we want to wrap
 * @param stat the stat struct for this file
 * @return the newly created object or NULL on error case
 **/
static inline pstd_fcache_file_t* _create_uncached_file(FILE* fp, const struct stat* stat)
{
	pstd_fcache_file_t* ret = (pstd_fcache_file_t*)pstd_mempool_alloc(sizeof(*ret));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the file cache reference");

	ret->cached = 0;
	ret->file = fp;
	ret->size = (size_t)stat->st_size;
	ret->offset = 0;

	LOG_DEBUG("Load file from disk");

	return ret;
}

/**
 * @brief invalidate the cache
 * @param entry the cache entry to invalidate
 * @return status code
 **/
static inline int _invalidate_cache(_cache_entry_t* entry)
{
	if(!entry->valid) return 0;

	int rc = 0;

	_data_size -= entry->size;

	if(entry->data != NULL)
	    free(entry->data);

#ifdef PSTD_FILE_CACHE_STRICT_KEY_COMP
	if(entry->filename != NULL)
	    free(entry->filename);
#endif

	entry->valid = 0;

	_lru_remove(entry->idx);
	return rc;
}

/**
 * @brief get the slot id from the hash code
 * @param hash the 128 bit hash code
 * @return the slot id
 **/
static inline uint32_t _hash_slot(const uint64_t* hash)
{
	/* Compute the slot id the entry should be in, we are actually doing a 128 bit modular */
	uint32_t slot = (uint32_t)((1ull<<32) % _cache_hash_size());
	slot = (uint32_t)(((uint64_t)slot * (uint64_t)slot) % _cache_hash_size());
	slot = (uint32_t)((slot * hash[1] + hash[0]) % _cache_hash_size());
	return slot;
}

/**
 * @brief check if the filename is loaded in cache, and if buf param is provided, load the stat info if possible
 * @param filename the filename to check
 * @param buf the stat buf if passed in then we will return the stat info if it's possible
 * @return error code for all error cases <br/>
 *         0 if not found and stat can not be loaded <br/>
 *         1 if not found but stat can be loaded <br/>
 *         2 if found and stat can be loaded <br/>
 **/
static inline int _is_in_cache(const char* filename, struct stat* buf)
{
	if(NULL == filename)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_thread_init())
	    ERROR_RETURN_LOG(int, "Cannot initialize the file cache for current thread");

	size_t f_len = strlen(filename);
	uint64_t hash[2];
	_murmurhash3_128(filename, f_len, hash);

	uint32_t slot = _hash_slot(hash);

	_cache_entry_t* entry = _cache_table + slot;

	int match_rc = _entry_matches(entry, hash, filename);
	if(1 == match_rc)
	{
		_lru_touch(entry->idx);
		if(NULL != buf) *buf = entry->stat;
		return 2;
	}

	if(match_rc == -1)
	{
		/* Get the file metadata */
		if(stat(filename, &entry->stat) < 0)
		    ERROR_RETURN_LOG_ERRNO(int, "Canot get the stat info of the file %s", filename);

		if(entry->stat.st_mtime >= entry->timestamp)
		{
			LOG_DEBUG("Cache entry %u is expired and the file on the disk has been touched since last read", entry->idx);
			if(NULL != buf) *buf = entry->stat;
			return 1;
		}

		LOG_DEBUG("Cache entry %u haven't been changed since last loaded, pushing the invalidate time to the furture", entry->idx);
		entry->timestamp = time(NULL);
		_lru_touch(entry->idx);
		if(NULL != buf) *buf = entry->stat;
		return 2;
	}

	return 0;
}

int pstd_fcache_is_in_cache(const char* filename)
{
	int rc = _is_in_cache(filename, NULL);
	if(ERROR_CODE(int) == rc)
	    return ERROR_CODE(int);
	else return rc == 2;
}

int pstd_fcache_stat(const char* filename, struct stat* buf)
{
	if(NULL == buf)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = _is_in_cache(filename, buf);
	if(ERROR_CODE(int) == rc)
	    return ERROR_CODE(int);

	if(rc == 1 || rc == 2)
	    return 0;

	if(stat(filename, buf) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Canot get the stat info of the file %s", filename);

	return 0;
}

pstd_fcache_file_t* pstd_fcache_open(const char* filename)
{
	if(NULL == filename)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(ERROR_CODE(int) == _ensure_thread_init())
	    ERROR_PTR_RETURN_LOG("Cannot initialize the file cache for current thread");

	size_t f_len = strlen(filename);

	uint64_t hash[2];
	_murmurhash3_128(filename, f_len, hash);
	uint32_t slot = _hash_slot(hash);

	_cache_entry_t* entry = _cache_table + slot;

	/* First we need to check if the file is alread in the cache, if yes, make a reference from the cache */
	int match_rc;
	if(1 == (match_rc = _entry_matches(entry, hash, filename)))
	{
		LOG_DEBUG("File %s is in cache, return the cached file", filename);
		return _create_cached_file(entry);
	}

	/* Get the file metadata */
	struct stat st;
	if(stat(filename, &st) < 0)
	    ERROR_PTR_RETURN_LOG("Canot get the stat info of the file %s", filename);

	/* Since we have 1 sec resolution, it may cause problem, so we want to make sure the time is strictly piror than the cache ts */
	if(match_rc == -1 && st.st_mtime < entry->timestamp)
	{
		LOG_DEBUG("The file %s haven't been changed since last we loaded, so pushing forward the timestamp to now", filename);
		entry->timestamp = time(NULL);
		entry->stat = st;
		LOG_DEBUG("File %s is in cache, return the cached file", filename);
		return _create_cached_file(entry);
	}

	/* Then we need to know the info about the file anyway, because we must read from disk */
	FILE* fp = fopen(filename, "rb");
	if(NULL == fp)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot open file %s", filename);


	/* This is safe, because the only place that can increase the value of the refcnt is the working thread rather than the
	 * IO thread. And this function will only be used in the working thread. Which means it's impossible for one entry that
	 * have a refcnt 0, and other increase the refcnt.
	 * The worst case here is the reference is dead already but the IO thread decref after this line is executed, which means
	 * we introduced an unncessecary uncached IO at this point. But it should be fine, because
	 *  1. Thread race condition is rare, because we only have IO thread and working thread involved
	 *  2. The hash conflict is rare
	 **/
	if((entry->valid && entry->refcnt > 0))
	{
		LOG_DEBUG("Unfortunately, the file %s has a conflict in the cache table, and"
		          "the other file is currently in use, so we unable to add it to cache",
		          filename);
		goto OPEN_UNCACHED;
	}

	if((size_t)st.st_size > _max_file_size())
	{
		LOG_DEBUG("The file size is larger than the cache size limit, so do not use the cache on the file"
		          "(actual: %zu, limit %u)", (size_t)st.st_size, _max_file_size());
		goto OPEN_UNCACHED;
	}

	/* Now lets load this file to cache */
	if(entry->valid && match_rc == 0)
	    LOG_DEBUG("We have see a cache conflict, so we will replace the old cache entry with the new one");

	if(ERROR_CODE(int) == _invalidate_cache(entry))
	    ERROR_LOG_GOTO(ERR, "Cannot invalidate the prevoius cache slot");

	/* Then we need to enforce the cache size limit */
	uint32_t cur;
	for(cur = _lru_last; cur != ERROR_CODE(uint32_t) && _data_size + (size_t)st.st_size > _max_cache_size();)
	{
		_cache_entry_t* victim = _cache_table + cur;
		cur = _cache_table[cur].lru_prev;

		/* As we previously mentioned, the worst case here should be the victim is accidentally survived.
		 * But this do not break the correctness, and this case is rare */
		if(victim->refcnt == 0)
		{
			LOG_DEBUG("Cache size limit reached, find victim %u to kill", victim->idx);
			if(ERROR_CODE(int) == _invalidate_cache(victim))
			    ERROR_LOG_GOTO(ERR, "Cannot invalidate the victim %u", victim->idx);
		}
		else
		    LOG_DEBUG("The victim %u survived because someone is using it", victim->idx);
	}

	if(_data_size + (size_t)st.st_size > _max_cache_size())
	{
		LOG_DEBUG("After killing all victims, we still don't have enough space for the new file, now giving up");
		goto OPEN_UNCACHED;
	}

	/* This is safe, because only worker thread can do both this and incref. Plus at this time no IO loop will be able to see this */
	entry->refcnt = 0;
	entry->hash[0] = hash[0];
	entry->hash[1] = hash[1];
	entry->timestamp = time(NULL);
	entry->stat = st;
	entry->size = (size_t)st.st_size;
#ifdef PSTD_FILE_CACHE_STRICT_KEY_COMP
	if(NULL == (entry->filename = (char*)malloc(f_len + 1)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the filename");
	memcpy(entry->filename, filename f_len + 1);
#endif

	if(NULL == (entry->data = (int8_t*)malloc(entry->size)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the data buffer");

	size_t off = 0;

	while(!feof(fp) && off < entry->size)
	{
		size_t rc;
		if(0 == (rc = fread(entry->data + off, 1, entry->size - off, fp)))
		    ERROR_LOG_ERRNO_GOTO(READ_ERR, "Cannot read the file to page");

		off += rc;
		continue;

READ_ERR:
		free(entry->data);
		goto ERR;
	}

	fclose(fp);

	entry->valid = 1;

	LOG_DEBUG("Cache slot %u has been occupied by file %s", slot, filename);

	_lru_add(entry->idx);
	_data_size += entry->size;


	LOG_DEBUG("File  %s is in cache, return the cached file", filename);
	return _create_cached_file(entry);

OPEN_UNCACHED:
	{
		pstd_fcache_file_t* ret = _create_uncached_file(fp, &st);
		if(ret != NULL) return ret;
		else LOG_ERROR("Cannot open uncached file reference");
	}
ERR:
	if(NULL != fp) fclose(fp);
	return NULL;
}

int pstd_fcache_close(pstd_fcache_file_t* file)
{
	if(NULL == file)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(file->cached)
	{
		if(file->cache->refcnt > 0)
		{
			/* This is the only place that the IO loop modify the refcnt, but we do a CAS here, so
			 * we make sure the counter is correct although the order of execution is not deterministic*/
			uint32_t new_val;
			do {
				new_val = file->cache->refcnt - 1;
			} while(!__sync_bool_compare_and_swap((uint32_t*)&file->cache->refcnt, new_val + 1, new_val));
		}
		else
		    ERROR_RETURN_LOG(int, "Code bug: refcnt is less than 0");
	}
	else
	{
		if(file->file != NULL)
		    fclose(file->file);
		else
		    ERROR_RETURN_LOG(int, "Code bug: file pointer is empty");
	}

	return pstd_mempool_free(file);
}

size_t pstd_fcache_size(const pstd_fcache_file_t* file)
{
	if(NULL == file)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	return file->size;
}

int pstd_fcache_eof(const pstd_fcache_file_t* file)
{
	if(NULL == file)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	return file->offset >= file->size;
}

size_t pstd_fcache_read(pstd_fcache_file_t* file, void* buf, size_t bufsize)
{
	if(NULL == file || NULL == buf)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	if(file->cached)
	{
		size_t ret = bufsize;

		if(ret + file->offset > file->cache->size)
		    ret = file->cache->size - file->offset;

		memcpy(buf, file->cache->data + file->offset, ret);
		file->offset += ret;
		return ret;
	}
	else
	{
		size_t rc = fread(buf, 1, bufsize, file->file);
		if(rc == 0 && ferror(file->file))
		    ERROR_RETURN_LOG_ERRNO(size_t, "Cannot read from the file");
		else
		{
			file->offset += rc;
			return rc;
		}
	}
}
