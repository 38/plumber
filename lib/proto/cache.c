/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include <package_config.h>
#include <proto/err.h>
#include <proto/ref.h>
#include <proto/type.h>
#include <proto/cache.h>
#include <proto/db.h>

/**
 * @brief one node in the hash table
 **/
typedef struct _node_t {
	char*           path;         /*!< the path to the protocol type description */
	uint64_t        hashcode;     /*!< the hashcode of the path */
	uint32_t        revdeps_cap;  /*!< the capacity of reverse dependecy array */
	uint32_t        revdeps_size; /*!< the size of the reverse dependecy list */
	uint32_t        revdeps_ok:1; /*!< indicates if we have succeessfully read the reverse dependey file, if not, we do not flush */
	uint32_t        sandbox:1;    /*!< if this is a sandbox update */
	uint32_t        type_dirty:1; /*!< if the type is dirty */
	uint32_t        rdep_dirty:1; /*!< if the reverse dependency dirty */
	uint32_t        own_type:1;   /*!< if the node owns the type object */
	proto_cache_node_data_dispose_func_t dispose_node_data;   /*!< how to dispose the node data */
	void*           node_data;    /*!< the actual node data */
	proto_type_t*   type;         /*!< the actual type object */
	char**          revdeps;      /*!< the reverse depedencies */
	struct _node_t* next;         /*!< the next pointer in the hash table */
} _node_t;

static _node_t* _hash_table[PROTO_CACHE_HASH_SIZE];

static const char* _root = PROTO_CACHE_DEFAULT_DB_ROOT;

static int _sandbox_enabled;

/**
 * @brief get the hash code from the typename
 * @param typename the name of the type
 * @param count the size of the typename
 * @note this is the MurmurHash2
 * @return the result hash code
 **/
static inline uint64_t _hash_func(const char* typename, size_t count)
{
	const uint64_t m = 0xc6a4a7935bd1e995ull;
	const int r = 47;

	uint64_t ret = 0;

	const uint8_t*   u8  = (const uint8_t*)typename;
	const uint64_t* u64 = (const uint64_t*)typename;
	u8 += count - (count % sizeof(uint64_t));

	for(;count >= sizeof(uint64_t); count -= sizeof(uint64_t))
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

/**
 * @brief create a new node that do not have the protocol type object and reverse dependency list
 * @param typename the typename of the newly created node
 * @param count the number of bytes in the typename
 * @param hashcode the hashcode for the typename
 * @note this function assume the typename is a valid pointer <br/>
 *       We design the function in this way, because we want to avoid recompute the hashcode
 * @return the newly created node or NULL on error cases
 **/
static inline _node_t* _node_new(const char* typename, size_t count, uint64_t hashcode)
{
	_node_t* ret = (_node_t*)calloc(1, sizeof(*ret));
	if(NULL == ret)
	    PROTO_ERR_RAISE_RETURN_PTR(ALLOC);

	ret->hashcode = hashcode;
	if(NULL == (ret->path = (char*)malloc(count + 1)))
	    PROTO_ERR_RAISE_GOTO(ERR, ALLOC);

	memcpy(ret->path, typename, count + 1);

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->path)
		    free(ret->path);
		free(ret);
	}

	return NULL;
}

/**
 * @brief dispose a used node and all the pointer that it owns
 * @param node the hash dnoe to dispose
 * @note this function assumes that the node is a valid pointer
 * @return status code
 **/
static inline int _node_free(_node_t* node)
{
	int rc = 0;

	if(NULL != node->type && node->own_type && ERROR_CODE(int) == proto_type_free(node->type))
	    rc = ERROR_CODE(int);

	if(NULL != node->path)
	    free(node->path);

	if(NULL != node->revdeps)
	{
		uint32_t i;
		for(i = 0; i < node->revdeps_size; i ++)
		    free(node->revdeps[i]);
		free(node->revdeps);
	}

	if(NULL != node->node_data && NULL != node->dispose_node_data)
	    rc = node->dispose_node_data(node->node_data);

	free(node);

	if(ERROR_CODE(int) == rc)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return rc;
}

/**
 * @brief check if the node has been deleted in cache
 * @param node the node to check
 * @return check result
 **/
static inline int _pending_deleted(const _node_t* node)
{
	if(_sandbox_enabled)
	    return node != NULL && node->type_dirty && node->type == NULL;
	else
	    return node != NULL && (!node->sandbox && node->type_dirty && node->type == NULL);
}

/**
 * @brief ensure the target directory exists
 * @param path the path to the file be written
 * @return status code
 **/
static inline int _ensure_dir(const char* path)
{
	static char buf[PATH_MAX];
	uint32_t i;
	for(i = 0;*path; path ++, i ++)
	{
		buf[i] = 0;
		if(*path == '/' && i > 0)
		{
			if(access(buf, R_OK) < 0 && mkdir(buf, 0775) < 0)
			    PROTO_ERR_RAISE_RETURN(int, FILEOP);
			else
			{
				struct stat stat_result;
				if(stat(buf, &stat_result) < 0)
				    PROTO_ERR_RAISE_RETURN(int, FILEOP);

				if(!S_ISDIR(stat_result.st_mode))
				    PROTO_ERR_RAISE_RETURN(int, FILEOP);
			}
		}
		buf[i] = *path;
	}

	return 0;
}

/**
 * @brief flush the reverse dependency information to disk
 * @param node the node to flush
 * @note this function will assume the node is a valid node, which is not the node that fail to create
 * @return status code
 **/
static inline int _flush_rdeps(_node_t* node)
{
	static char pathbuf[PATH_MAX];
	int needs_update = node->rdep_dirty && node->revdeps_ok;
	int needs_delete = _pending_deleted(node);
	if(node->sandbox || (!needs_update && !needs_delete))
	    return 0;

	snprintf(pathbuf, sizeof(pathbuf), "%s/%s"PROTO_CACHE_REVDEP_FILE_SUFFIX, _root, node->path);

	if(!_pending_deleted(node))
	{
		if(node->revdeps == NULL) PROTO_ERR_RAISE_RETURN(int, BUG);

		if(ERROR_CODE(int) == _ensure_dir(pathbuf))
		    PROTO_ERR_RAISE_RETURN(int, FAIL);

		FILE* fp = fopen(pathbuf, "w");
		if(NULL == fp)
		    PROTO_ERR_RAISE_RETURN(int, FILEOP);

		uint32_t i;
		for(i = 0; i < node->revdeps_size; i ++)
		    fprintf(fp, "%s\n", node->revdeps[i]);
		fclose(fp);
		node->rdep_dirty = 0u;
	}
	else if(access(pathbuf, R_OK) >= 0 &&  unlink(pathbuf) < 0) PROTO_ERR_RAISE_RETURN(int, FILEOP);

	return 0;
}
/**
 * @brief flush the type object to disk
 * @param node the node to flush
 * @note this function assuems the node is valid
 * @return status code
 **/
static inline int _flush_type(_node_t* node)
{
	char pathbuf[PATH_MAX];
	if(node->sandbox || !node->type_dirty) return 0;

	snprintf(pathbuf, sizeof(pathbuf), "%s/%s"PROTO_CACHE_PROTO_FILE_SUFFIX, _root, node->path);

	if(!_pending_deleted(node))
	{
		if(ERROR_CODE(int) == _ensure_dir(pathbuf) || ERROR_CODE(int) == proto_type_dump(node->type, pathbuf))
		    PROTO_ERR_RAISE_RETURN(int, FAIL);

		node->type_dirty = 0u;
	}
	else if(access(pathbuf, R_OK) >= 0 && unlink(pathbuf) < 0) PROTO_ERR_RAISE_RETURN(int, FILEOP);

	return 0;
}

/**
 * @brief ensure the reverse dependecy array has at least one empty slot
 * @param node the node to ensure
 * @note this function all assume the node is a valid node
 * @return status code
 **/
static inline int _ensure_revdeps_cap(_node_t* node)
{
	if(node->revdeps == NULL)
	    node->revdeps_cap = node->revdeps_size = 0;

	if(node->revdeps_size >= node->revdeps_cap)
	{
		uint32_t next_size = node->revdeps_cap * 2;
		char**   new_array = NULL;
		if(next_size == 0)
		    next_size = PROTO_CACHE_REVDEP_INIT_SIZE,
		    new_array = (char**)malloc(sizeof(*new_array) * (next_size + 1));
		else
		    new_array = (char**)realloc(node->revdeps, sizeof(*new_array) * (next_size + 1));

		if(NULL == new_array)
		    PROTO_ERR_RAISE_RETURN(int, ALLOC);

		node->revdeps_cap = next_size;
		node->revdeps = new_array;
	}

	return 0;
}

/**
 * @brief find the node under the type name, and (if required) create a new node for the given name
 * @param typename the name of the type
 * @param create indicates if we need to create a new node if the typename doesn't exist in the hash table
 * @return the node for this type, NULL on error case or not found
 * @note this function's return value is tricky, when the function is not required to create new node, NULL indicates the node
 *       is not in the hash table, and there will be no error cases. However, if the function is in creation mode, the NULL
 *       pointer return value indicates the function fails to create the node, thus it's an error case and error stack should be set
 *
 **/
static inline _node_t* _hash_find(const char* typename, int create)
{
	size_t   size = strlen(typename);
	uint64_t hash = _hash_func(typename, size);
	uint32_t slot = (uint32_t)(hash % PROTO_CACHE_HASH_SIZE);

	_node_t* ret = _hash_table[slot];
	for(;NULL != ret && (ret->hashcode != hash || strcmp(ret->path, typename) != 0); ret = ret->next);

	if(NULL == ret && create)
	{

		_node_t* new_node = _node_new(typename, size, hash);
		if(NULL == new_node)
		    PROTO_ERR_RAISE_RETURN_PTR(FAIL);
		new_node->next = _hash_table[slot];
		_hash_table[slot] = new_node;
		ret = new_node;
	}

	return ret;
}

/**
 * @brief delete a node from the hash table
 * @param node the node to delete
 * @note this function assume the valididty of the pointer
 * @return status code
 **/
static inline int _hash_delete(_node_t* node)
{
	uint32_t slot = (uint32_t)(node->hashcode % PROTO_CACHE_HASH_SIZE);

	_node_t* prev = NULL;
	if(_hash_table[slot] != node)
	{
		for(prev = _hash_table[slot]; NULL != prev && prev->next != node; prev = prev->next);

		/* This means the node is not in the hash table */
		if(NULL == prev)
		    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);
	}

	if(prev != NULL) prev->next = node->next;
	else _hash_table[slot] = node->next;

	if(ERROR_CODE(int) == _node_free(node))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return 0;
}

/**
 * @brief remove all the nodes in the hash table, this will be used when the cache is fianlized or
 *        there's a new protodb root
 * @return status code
 **/
static inline int _clear_cache()
{
	int rc = 0;
	uint32_t i;
	for(i = 0; i < PROTO_CACHE_HASH_SIZE; i ++)
	{
		_node_t* ptr;
		for(ptr = _hash_table[i]; NULL != ptr;)
		{
			_node_t* cur = ptr;
			ptr = ptr->next;
			if(ERROR_CODE(int) == _node_free(cur))
			    rc = ERROR_CODE(int);
		}
		_hash_table[i] = NULL;
	}
	return rc;
}

static inline _node_t* _get_hash_node(const char* typename, const char* pwd);
static inline _node_t* _get_hash_node_range_no_raise(const char* typename, const char* pwd_begin, const char* pwd_end);

/**
 * @brief load the reverse dependency information for the node
 * @note if the information is already in memory, just return
 * @param node the node to load
 * @return status code
 **/
static int _load_revdeps(_node_t* node)
{
	if(node->revdeps != NULL) return 0;

	char buf[PATH_MAX];
	snprintf(buf, sizeof(buf), "%s/%s"PROTO_CACHE_REVDEP_FILE_SUFFIX, _root, node->path);
	FILE* fp = fopen(buf, "r");

	if(NULL != fp)
	{
		while(NULL != fgets(buf, sizeof(buf), fp))
		{
			size_t len = strlen(buf);
			for(;len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'); buf[--len] = 0);
			if(ERROR_CODE(int) == _ensure_revdeps_cap(node))
			    PROTO_ERR_RAISE_GOTO(ERR, FAIL);
			uint32_t idx = node->revdeps_size;
			if(NULL == (node->revdeps[idx] = (char*)malloc(len + 1)))
			    PROTO_ERR_RAISE_GOTO(ERR, READ);
			memcpy(node->revdeps[idx], buf, len + 1);
			node->revdeps_size ++;
		}
		fclose(fp);
	}

	if(node->revdeps == NULL && ERROR_CODE(int) == _ensure_revdeps_cap(node))
	    PROTO_ERR_RAISE_GOTO(ERR, FAIL);

	node->revdeps[node->revdeps_size] = NULL;
	node->revdeps_ok = 1;
	node->rdep_dirty = 0u;


	return 0;
ERR:
	if(NULL != fp) fclose(fp);
	/* Because the revdeps buffer is owned by the hastable, do not dispose at this time */
	return ERROR_CODE(int);
}

/**
 * @brief add a reverse dependency information from this_type to that_type,
 *        which means that_type has made a reference to this type
 * @param this_type the type which that_type depends on
 * @param that_type the type which depens on this_type
 * @note all the typenames are absolute typename
 * @return status code
 **/
static inline int _revdep_add(const char* this_type, const char* that_type)
{
	if(this_type == NULL || that_type == NULL) PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	_node_t* node = _get_hash_node(this_type, NULL);
	if(NULL == node) PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(_pending_deleted(node)) return 0;

	if(ERROR_CODE(int) == _load_revdeps(node)) PROTO_ERR_RAISE_RETURN(int, FAIL);

	/* We don't allow to modify a incompleted reverse dependency table, because it may cuase problem */
	if(!node->revdeps_ok) PROTO_ERR_RAISE_RETURN(int, DISALLOWED);

	uint32_t i;
	for(i = 0; i < node->revdeps_size; i ++)
	    if(strcmp(that_type, node->revdeps[i]) == 0)
	        return 0;

	if(ERROR_CODE(int) == _ensure_revdeps_cap(node)) PROTO_ERR_RAISE_RETURN(int, FAIL);

	size_t len = strlen(that_type);
	uint32_t idx = node->revdeps_size;
	if(NULL == (node->revdeps[idx] = (char*)malloc(len + 1)))
	    PROTO_ERR_RAISE_RETURN(int, ALLOC);

	memcpy(node->revdeps[idx], that_type, len + 1);

	node->revdeps[++node->revdeps_size] = NULL;
	node->rdep_dirty = 1;

	return 0;
}

/**
 * @brief remove a reverse dependency information
 * @param this_type the type which that_type depends on
 * @param that_type the type which depdens on this_type
 * @note all the typenames are absolute typename
 * @return status code
 **/
static inline int _revdep_remove(const char* this_type, const char* that_type)
{
	if(this_type == NULL || that_type == NULL) PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	_node_t* node = _get_hash_node(this_type, NULL);
	if(NULL == node) PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(_pending_deleted(node)) return 0;

	if(ERROR_CODE(int) == _load_revdeps(node)) PROTO_ERR_RAISE_RETURN(int, FAIL);

	/* We don't allow to modify a incompleted reverse dependency table, because it may cuase problem */
	if(!node->revdeps_ok) PROTO_ERR_RAISE_RETURN(int, DISALLOWED);

	uint32_t i;
	for(i = 0; i < node->revdeps_size && strcmp(that_type, node->revdeps[i]) != 0; i ++);
	if(i == node->revdeps_size) return 0;
	free(node->revdeps[i]);

	if(i < node->revdeps_size - 1)
	    memmove(node->revdeps + i, node->revdeps + i + 1, sizeof(node->revdeps[0]) * (node->revdeps_size - i - 1));

	node->revdeps[--node->revdeps_size] = NULL;
	node->rdep_dirty = 1;
	node->sandbox = (_sandbox_enabled != 0);
	return 0;
}
/**
 * @brief update the reverse dependency, we either add or remove a reverse dependency entry for the target
 *        type
 * @param typename the typename to update
 * @param type the type object we want to update against
 * @param add if this type object is to be added
 * @return status code
 **/
static inline int _update_rdep(const char* typename, const proto_type_t* type, int add)
{
	size_t sz = strlen(typename);
	for(;sz > 0 && typename[sz] != '/'; sz --);
	const char* pwd_end = typename + sz;

	uint32_t i, nent = proto_type_get_size(type);
	if(nent == ERROR_CODE(uint32_t))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	for(i = 0; i < nent; i ++)
	{
		const proto_type_entity_t* ent = proto_type_get_entity(type, i);
		if(NULL == ent) PROTO_ERR_RAISE_RETURN(int, FAIL);

		if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
		{
			const char* depname = proto_ref_typeref_get_path(ent->type_ref);
			if(NULL == depname) PROTO_ERR_RAISE_RETURN(int, FAIL);

			_node_t* node = _get_hash_node_range_no_raise(depname, typename, pwd_end);
			if(NULL == node || _pending_deleted(node)) continue;

			if(!add)
			{
				if(ERROR_CODE(int) == _revdep_remove(node->path, typename))
				    PROTO_ERR_RAISE_RETURN(int, FAIL);
			}
			else if(ERROR_CODE(int) == _revdep_add(node->path, typename))
			    PROTO_ERR_RAISE_RETURN(int, FAIL);
		}
	}
	return 0;
}


int proto_cache_flush()
{
	if(_sandbox_enabled)
	    PROTO_ERR_RAISE_RETURN(int, DISALLOWED);

	int rc = 0;
	uint32_t i;
	for(i = 0; i < PROTO_CACHE_HASH_SIZE; i ++)
	{
		_node_t* ptr;
		for(ptr = _hash_table[i]; NULL != ptr;)
		{
			_node_t* cur = ptr;
			ptr = ptr->next;
			if(ERROR_CODE(int) == _flush_rdeps(cur))
			    rc = ERROR_CODE(int);
			if(ERROR_CODE(int) == _flush_type(cur))
			    rc = ERROR_CODE(int);
			if(_pending_deleted(ptr) && ERROR_CODE(int) == _hash_delete(cur))
			    rc = ERROR_CODE(int);
		}
	}
	return rc;
}

int proto_cache_init()
{
	_sandbox_enabled = 0;
	return 0;
}

int proto_cache_finalize()
{
	int rc = 0;

	rc = _clear_cache();

	if(ERROR_CODE(int) == rc)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return 0;
}

int proto_cache_set_root(const char* root)
{
	if(NULL == root || _sandbox_enabled)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	if(ERROR_CODE(int) == _clear_cache())
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	_root = root;

	return 0;
}

/**
 * @brief is the given absolute type name exist on the disk
 * @param path the filesystem absolute path (means including the protodb root path prefix)
 * @return if this type exists
 **/
static inline int _proto_file_exist(const char* path)
{
	if(access(path, R_OK) < 0)
	    return 0;

	struct stat stat_result;
	if(stat(path, &stat_result) < 0)
	    return 0;

	if(!S_ISREG(stat_result.st_mode))
	    return 0;

	return 1;
}
/**
 * @brief find the hash node for the type with given prefix and typename, the typename is given by an address range
 * @param prefix_begin the prefix of the typename
 * @param prefix_end the prefix end address of the type name:
 * @param name  typename
 * @param result the result buffer
 * @note the range of the name buffer is [name_begin, name_end)
 * @return the number of hash node found in the type system (including the one not in memory but newly created at this time)
 *         or error code on error cases
 **/
static inline int _find_hash_node_with_prefix(const char* prefix_begin, const char* prefix_end, const char* name, _node_t** result)
{
	static __thread char pathbuf[PATH_MAX];
	size_t root_len = strlen(_root);
	size_t prefix_len  = prefix_begin == NULL ? 0 : (size_t)(prefix_end - prefix_begin);
	size_t name_len = strlen(name);
	static const char proto_file_suffix[] = PROTO_CACHE_PROTO_FILE_SUFFIX;

	if(root_len + 1 + prefix_len + 1 + name_len + 1 + sizeof(proto_file_suffix) > sizeof(pathbuf))
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	memcpy(pathbuf, _root, root_len);
	pathbuf[root_len] = '/';

	char* prefix_buf = pathbuf + root_len + 1;
	char* name_buf = prefix_len > 0 ? prefix_buf + prefix_len + 1 : prefix_buf;
	char* suffix_buf = name_buf + name_len;
	if(prefix_len > 0)
	{
		memcpy(prefix_buf, prefix_begin, prefix_len);
		name_buf[-1] = '/';
	}
	memcpy(name_buf, name, name_len);
	suffix_buf[0] = 0;

	/* Try to find the node in memory */
	if(NULL != (*result = _hash_find(prefix_buf, 0)) && !_pending_deleted(*result)) return 1;

	memcpy(suffix_buf, proto_file_suffix, sizeof(proto_file_suffix));

	/* Not found, ok, let's look up it on the disk */
	if(!_pending_deleted(*result) && _proto_file_exist(pathbuf))
	{
		suffix_buf[0] = 0;
		if(NULL != (*result = _hash_find(prefix_buf, 1))) return 1;

		PROTO_ERR_RAISE_RETURN(int, FAIL);
	}

	return 0;
}

/**
 * @brief get the hash node for the given type, the type name may be a relative path based on the pwd param
 *        if pwd is NULL means do not search for the relative path
 * @param typename the name of the type
 * @param pwd_begin the base dir we want to search for the relative typename, this is the begin address
 * @param pwd_end   the base dir we want to search for the rel-typename, this is tne end address. The pwd range is [pwd_begin, pwd_end)
 * @param raise if we want to raise NOT_FOUND error
 * @return the node
 * @note If the type is defined in the system, but not loaded, the function will create a new node for it.
 *       But this operation won't load any data, just make an empty node for this type<br/>
 *       If the type is already in the cache, just return the existing node <br/>
 *       Otherwise this function should return NULL, and this could be an error case
 **/
static inline _node_t* _get_hash_node_impl(const char* typename, const char* pwd_begin, const char* pwd_end, int raise)
{
	_node_t* ret;
	int rc = 0;

	/* First attempt the relative path in the cache */
	if(pwd_begin != NULL && ERROR_CODE(int) == (rc = _find_hash_node_with_prefix(pwd_begin, pwd_end, typename, &ret)))
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);
	if(rc > 0) goto RET;

	if(ERROR_CODE(int) == (rc = _find_hash_node_with_prefix(NULL, NULL, typename, &ret)))
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);
	if(rc > 0) goto RET;

	if(raise) PROTO_ERR_RAISE_RETURN_PTR(NOT_FOUND);
	else return NULL;

RET:
	/* If this node has been changed in the sandbox mode, and we are currently not in the sandbox mode, discard it */
	if(!_sandbox_enabled && ret->sandbox)
	{
		ret->revdeps_ok = 0;
		ret->sandbox = 0;
		ret->type_dirty = 0u;
		ret->rdep_dirty = 0u;
		if(ret->node_data != NULL && ret->dispose_node_data != NULL &&
		   ERROR_CODE(int) == ret->dispose_node_data(ret->node_data))
		        PROTO_ERR_RAISE_RETURN_PTR(FAIL);

		ret->node_data = NULL;
		ret->dispose_node_data = NULL;

		if(ret->own_type && ret->type != NULL && proto_type_free(ret->type) == ERROR_CODE(int))
		    PROTO_ERR_RAISE_RETURN_PTR(FAIL);
		ret->own_type = 0u;
		ret->type = NULL;

		if(ret->revdeps != NULL)
		{
			uint32_t i;
			for(i = 0; i < ret->revdeps_size; i ++)
			    if(NULL != ret->revdeps[i])
			        free(ret->revdeps[i]);
			free(ret->revdeps);
			ret->revdeps = NULL;
		}
		ret->revdeps_cap = ret->revdeps_size = 0;
	}
	return ret;
}

/**
 * @brief find the hash node from the caching system under the given pwd,
 *        If the type exists in the fileystem but not in memory create a new node for it.
 * @param typename the type name to query
 * @param pwd the current working directory
 * @return the node found, or NULL on error
 **/
static inline _node_t* _get_hash_node(const char* typename, const char* pwd)
{
	const char* pwd_end = pwd == NULL ? NULL : pwd + strlen(pwd);
	return _get_hash_node_impl(typename, pwd, pwd_end, 0);
}

/**
 * @brief this is the no raising version of _get_hash_node, which do not raise the NOT_FOUND error
 * @param typename the typename to query
 * @param pwd the current working directory
 * @return the node found, NULL if not found
 **/
static inline _node_t* _get_hash_node_no_raise(const char* typename, const char* pwd)
{
	const char* pwd_end = pwd == NULL ? NULL : pwd + strlen(pwd);
	return _get_hash_node_impl(typename, pwd, pwd_end, 0);
}

/**
 * @brief this is the address range version of _get_hash_node, which takes a pair of pointers as the range as the
 *        pwd input
 * @param typename  the typename
 * @param pwd_begin the current working directory begining address
 * @param pwd_end   the current working directory ending address
 * @return the node found, NULL on error case
 **/
static inline _node_t* _get_hash_node_range_no_raise(const char* typename, const char* pwd_begin, const char* pwd_end)
{
	return _get_hash_node_impl(typename, pwd_begin, pwd_end, 0);
}

int proto_cache_full_type_name_exist(const char* typename)
{
	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	_node_t* ret;

	if(NULL != (ret = _get_hash_node_no_raise(typename, NULL)) && !_pending_deleted(ret))
	    return 1;

	/* It's possible it's in the cache and was pending deleted, in this case, although _get_hash_node_no_raise returns NULL
	 * and the proto file is on the disk, we still can not say it's exist, because it's virtually deleted */
	if(NULL != (ret = _hash_find(typename, 0)) && _pending_deleted(ret))
	    return 0;

	char pathbuf[PATH_MAX];
	/* Not found in cache, so try disk */
	snprintf(pathbuf, sizeof(pathbuf), "%s/%s"PROTO_CACHE_PROTO_FILE_SUFFIX, _root, typename);
	if(!_pending_deleted(ret) && _proto_file_exist(pathbuf)) return 1;

	return 0;
}

const char* proto_cache_full_name(const char* typename, const char* pwd)
{
	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	_node_t* node = _get_hash_node(typename, pwd);
	if(NULL == node || _pending_deleted(node))
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	return node->path;
}

int proto_cache_put(const char* typename, proto_type_t* proto)
{
	if(NULL == typename || NULL == proto)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	char namebuf[PATH_MAX];

	snprintf(namebuf, sizeof(namebuf), "%s/%s"PROTO_CACHE_PROTO_FILE_SUFFIX, _root, typename);
	_node_t* node = _hash_find(typename, 1);
	if(NULL == node)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(!_pending_deleted(node) && _proto_file_exist(namebuf))
	{
		if(node->type == NULL && NULL == (node->type = (proto_type_t*)proto_cache_get_type(typename, NULL, NULL)))
		    PROTO_ERR_RAISE_RETURN(int, FAIL);

		if(ERROR_CODE(int) == _update_rdep(typename, node->type, 0))
		    PROTO_ERR_RAISE_RETURN(int, FAIL);

		if(node->type != NULL && node->own_type && ERROR_CODE(int) == proto_type_free(node->type))
		        PROTO_ERR_RAISE_RETURN(int, FAIL);
		node->type = NULL;

		if(node->node_data != NULL && node->dispose_node_data != NULL &&
		   node->dispose_node_data(node->node_data) == ERROR_CODE(int))
		    PROTO_ERR_RAISE_RETURN(int, FAIL);
		node->node_data = NULL;
	}

	/* then we need to take over the provided protocol type object */
	node->type = proto;
	node->sandbox = (_sandbox_enabled != 0);
	/* If sandbox is enabled, we do not own the type anymore */
	node->own_type = (0 == _sandbox_enabled);
	node->type_dirty = 1u;

	if(ERROR_CODE(int) == _update_rdep(typename, proto, 1))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	return 0;
}

int proto_cache_delete(const char* typename)
{
	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	char namebuf[PATH_MAX];
	snprintf(namebuf, sizeof(namebuf), "%s/%s"PROTO_CACHE_PROTO_FILE_SUFFIX, _root, typename);

	/* In sandbox mode, we need create a node and mark it as virtually deleted */
	_node_t* node = _hash_find(typename, _sandbox_enabled ? 1 : 0);

	if(NULL == node)
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(_proto_file_exist(namebuf) && node->type == NULL && NULL == (node->type = (proto_type_t*)proto_cache_get_type(typename, NULL, NULL)))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(node->type != NULL && ERROR_CODE(int) == _update_rdep(typename, node->type, 0))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(node->own_type && node->type && ERROR_CODE(int) == proto_type_free(node->type))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	node->type = NULL;
	node->type_dirty = 1;
	node->sandbox = (_sandbox_enabled != 0);

	return 0;
}

const proto_type_t* proto_cache_get_type(const char* typename, const char* pwd, void** data)
{
	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	_node_t* node = _get_hash_node(typename, pwd);
	if(NULL == node || _pending_deleted(node))
	    PROTO_ERR_RAISE_RETURN_PTR(NOT_FOUND);

	if(node->type == NULL)
	{
		char pathbuf[PATH_MAX];
		snprintf(pathbuf, sizeof(pathbuf), "%s/%s"PROTO_CACHE_PROTO_FILE_SUFFIX, _root, node->path);
		if(NULL == (node->type = proto_type_load(pathbuf)))
		    PROTO_ERR_RAISE_RETURN_PTR(FAIL);
		node->own_type = 1u;
		node->type_dirty = 0u;
	}

	if(NULL != data)
	    *data = node->node_data;

	return node->type;
}

char const* const* proto_cache_revdep_get(const char* typename, const char* pwd)
{
	if(NULL == typename)
	    PROTO_ERR_RAISE_RETURN_PTR(ARGUMENT);

	_node_t* node = _get_hash_node(typename, pwd);

	if(NULL == node || _pending_deleted(node))
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	if(ERROR_CODE(int) == _load_revdeps(node))
	    PROTO_ERR_RAISE_RETURN_PTR(FAIL);

	return (char const* const*)node->revdeps;
}


int proto_cache_attach_type_data(const char* typename, const char* pwd, proto_cache_node_data_dispose_func_t dispose_cb, void* data)
{
	if(typename == NULL)
	    PROTO_ERR_RAISE_RETURN(int, ARGUMENT);

	_node_t* node = _get_hash_node(typename, pwd);
	if(NULL == node || _pending_deleted(node))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	if(node->node_data != NULL && node->dispose_node_data != NULL &&
	   ERROR_CODE(int) == node->dispose_node_data(node->node_data))
	    PROTO_ERR_RAISE_RETURN(int, FAIL);

	node->node_data = data;
	node->dispose_node_data = dispose_cb;

	return 0;
}

void proto_cache_sandbox_mode(int mode)
{
	_sandbox_enabled = (mode != 0);
}

const char* proto_cache_get_root()
{
	return _root;
}
