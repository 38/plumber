/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

#include <error.h>

#include <pstd.h>
#include <pstd/types/file.h>

/**
 * @brief the actual data structure for a file reference
 **/
struct _pstd_file_t {
	char*       filename;        /*!< the target file name */
	uint32_t    committed:1;     /*!< if the object has been committed */
	uint32_t    stat_ready:1;    /*!< if we already get the stat of the file */
	struct stat stat;            /*!< the cached stat information */
	uintptr_t  __padding__[0];
	char       _def_buf[128];   /*!< the default filename buf */
};

pstd_file_t* pstd_file_new(const char* filename)
{
	if(NULL == filename)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	pstd_file_t* ret = (pstd_file_t*)pstd_mempool_alloc(sizeof(pstd_file_t));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the RLS file object");

	size_t len = strlen(filename);
	if(len < sizeof(ret->_def_buf))
	    ret->filename = ret->_def_buf;
	else if(NULL == (ret->filename = (char*)malloc(len + 1)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate filename buffer");

	memcpy(ret->filename, filename, len + 1);
	ret->committed = 0;
	ret->stat_ready = 0;
	LOG_DEBUG("File RLS object for filename %s has been created", filename);
	return ret;
ERR:
	pstd_mempool_free(ret);
	return NULL;
}

/**
 * @brief the implementation for free
 * @param file the file object to free
 * @param allow_committed indicates if we want to dispose the committed object
 * @return status code
 **/
static inline int _free_impl(pstd_file_t* file, int allow_committed)
{
	if(NULL == file)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(file->committed && !allow_committed)
	    ERROR_RETURN_LOG(int, "Cannot dispose a committed file RLS object");

	LOG_DEBUG("File RLS for filename %s is being disposed", file->filename);

	if(file->filename != file->_def_buf)
	    free(file->filename);

	return pstd_mempool_free(file);
}

int pstd_file_free(pstd_file_t* file)
{
	return _free_impl(file, 0);
}

const pstd_file_t* pstd_file_from_rls(scope_token_t token)
{
	if(ERROR_CODE(scope_token_t) == token)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const pstd_file_t* ret = (const pstd_file_t*)pstd_scope_get(token);
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot get RLS token %u from the request local scope", token);

	LOG_DEBUG("File RLS token %u has been acuired", token);

	return ret;
}

/**
 * @brief read and cache the file's stat information
 * @param file the file object to read
 * @return status code
 **/
static inline int _read_stat(const pstd_file_t* file)
{
	if(file->stat_ready == 1)
	{
		LOG_DEBUG("The file RLS object has cached stat metadata, reuse it");
		return 0;
	}

	LOG_DEBUG("The file RLS object do not have stat metadata cached in memory, call the system call");

#ifdef PSTD_FILE_NO_CACHE
	if(stat(file->filename, (struct stat*)&file->stat) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "System call stat returns an unexpected error");
#else
	if(pstd_fcache_stat(file->filename, (struct stat*)&file->stat) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot get the file metadata from file cache");
#endif

	/* dirty hack, but we still need to do this */
	((pstd_file_t*)file)->stat_ready = 1;

	return 0;
}

int pstd_file_exist(const pstd_file_t* file)
{
	if(NULL == file)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const char* name = file->filename;

#ifndef PSTD_FILE_NO_CACHE
	if(pstd_fcache_is_in_cache(name) > 0)
	    return 1;
#endif

	if(access(name, R_OK) != F_OK)
	{
		if(errno == EACCES || errno == ENOTDIR || errno == ENOENT)
		{
			LOG_DEBUG("The filename referred by the file RLS is not accessible: %s", file->filename);
			return 0;
		}
		ERROR_RETURN_LOG_ERRNO(int, "System call access returns an unexpected error");
	}

	if(ERROR_CODE(int) == _read_stat(file))
	    ERROR_RETURN_LOG(int, "Cannot read the file information");

	if(S_ISREG(file->stat.st_mode))
	{
		LOG_DEBUG("File referred by the file RLS exists: %s", file->filename);
		return 1;
	}

	LOG_DEBUG("File referred by the file RLS is not a regular file: %s", file->filename);
	return 0;
}

size_t pstd_file_size(const pstd_file_t* file)
{
	if(NULL == file)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	if(ERROR_CODE(int) == _read_stat(file))
	    ERROR_RETURN_LOG(size_t, "Cannot read the file information");

	return (size_t)file->stat.st_size;
}


FILE* pstd_file_open(const pstd_file_t* file, const char* mode)
{
	if(NULL == file || NULL == mode)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	FILE* ret = fopen(file->filename, mode);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot open file %s", file->filename);

	LOG_DEBUG("File RLS object for %s has been opened as stdio FILE", file->filename);

	/* Because the stdio FILE may modify the file, so we may want to reload the stat again */
	((pstd_file_t*)file)->stat_ready = 0;

	return ret;
}

/**
 * @brief the dispose callback called by the RLS infrstructure
 * @param mem the file RLS object to dispose
 * @return status code
 **/
static inline int _free(void* mem)
{
	pstd_file_t* file = (pstd_file_t*)mem;
	LOG_DEBUG("RLS file %s is disposed", file->filename);
	return _free_impl(file, 1);
}

/**
 * @brief the open stream callback used by the RLS infrastructure
 * @param mem the file RLS object to open
 * @return the stream handle, in this case it's a stdio FILE object
 **/
static inline void* _open(const void* mem)
{
	const pstd_file_t* file = (const pstd_file_t*)mem;
#ifdef PSTD_FILE_NO_CACHE
	FILE* ret = fopen(file->filename, "rb");
#else
	pstd_fcache_file_t* ret = pstd_fcache_open(file->filename);
#endif
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot open file %s", file->filename);

	LOG_DEBUG("RLS file %s is opened as a byte stream", file->filename);

	return ret;
}

/**
 * @brief the close stream callback used by the RLS infrastructure
 * @param stream_mem the stream handle, in this case it should be a stdio FILE
 * @return status code
 **/
static inline int _close(void* stream_mem)
{
#ifdef PSTD_FILE_NO_CACHE
	FILE* stream = (FILE*)stream_mem;
	fclose(stream);
#else
	pstd_fcache_file_t* stream = (pstd_fcache_file_t*)stream_mem;
	if(ERROR_CODE(int) == pstd_fcache_close(stream))
	    ERROR_RETURN_LOG(int, "Cannot close the file cache reference");
#endif

	LOG_DEBUG("The byte stream interface for RLS file has been closed");
	return 0;
}

/**
 * @brief the end-of-stream callback used by the RLS infrastructure
 * @param stream_mem the stream handle, in this case it should be a stdio FILE
 * return status code or check result
 **/
static inline int _eos(const void* stream_mem)
{
#ifdef PSTD_FILE_NO_CACHE
	FILE* stream = (FILE*)stream_mem;
	int rc = feof(stream);
	if(rc < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot check if the stream gets the end");
	return rc > 0;
#else
	pstd_fcache_file_t* stream = (pstd_fcache_file_t*)stream_mem;
	return pstd_fcache_eof(stream);
#endif
}

/**
 * @brief the callback read bytes from the byte stream interface for a file RLS, called by RLS infrastructure
 * @param stream_mem the stream handle, which should be stdio FILE
 * @param buf the buffer we will return the result
 * @param count the number of bytes that needs to be read
 * @return the number of bytes has been read, or error code
 **/
static inline size_t _read(void* __restrict stream_mem, void* __restrict buf, size_t count)
{
#ifdef PSTD_FILE_NO_CACHE
	FILE* stream = (FILE*)stream_mem;
	size_t rc = fread(buf, 1, count, stream);
	if(rc == 0 && ferror(stream))
	    ERROR_RETURN_LOG_ERRNO(size_t, "Cannot read file the RLS file stream");

	LOG_DEBUG("%zu bytes has been read from RLS file byte stream interface", rc);

	return rc;
#else
	pstd_fcache_file_t* stream = (pstd_fcache_file_t*)stream_mem;

	return pstd_fcache_read(stream, buf, count);
#endif
}

scope_token_t pstd_file_commit(pstd_file_t* file)
{
	if(NULL == file || file->committed)
	    ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	scope_entity_t ent = {
		.data = file,
		.free_func = _free,
		.copy_func = NULL,
		.open_func = _open,
		.close_func = _close,
		.eos_func = _eos,
		.read_func = _read
	};

	scope_token_t ret = pstd_scope_add(&ent);

	if(ERROR_CODE(scope_token_t) == ret)
	    ERROR_RETURN_LOG(scope_token_t, "Cannot add the entity to the scope");

	return ret;
}

const char* pstd_file_name(const pstd_file_t* file)
{
	if(NULL == file)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	return file->filename;
}
