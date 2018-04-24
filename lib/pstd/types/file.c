/**
 * Copyright (C) 2017, Hao Hou
 * Copyright (C) 2018, Feng Liu
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
#include <utils/hash/murmurhash3.h>

/**
 * @brief the actual data structure for a file reference
 **/
struct _pstd_file_t {
	char*       filename;        /*!< the target file name */
	uint32_t    committed:1;     /*!< if the object has been committed */
	uint32_t    stat_ready:1;    /*!< if we already get the stat of the file */
	uint32_t    partial:1;       /*! We want to access part of the file */
	size_t      part_beg;        /*!< The begining of the partial content */
	size_t      part_size;       /*!< The size of the part that is accessable */
	struct stat stat;            /*!< the cached stat information */
	uintpad_t  __padding__[0];
	char       _def_buf[128];   /*!< the default filename buf */
};

/**
 * @brief The opened file stream
 **/
typedef struct {
#ifdef PSTD_FILE_NO_CACHE
	FILE* file;     /*!< The file pointer */
#else
	pstd_fcache_file_t* file;  /*!< The file pointer */
#endif
	size_t remaining;  /*!< The remaining bytes to read (for ranged file) */
} _stream_t;

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
	ret->partial = 0;
	LOG_DEBUG("File RLS object for filename %s has been created", filename);
	return ret;
ERR:
	pstd_mempool_free(ret);
	return NULL;
}

int pstd_file_set_range(pstd_file_t* file, size_t begin, size_t end)
{
	if(NULL == file || end < begin)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	file->partial = 1;
	file->part_beg = begin;
	file->part_size = (size_t)(end - begin);

	return 0;
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	/* We should allow the interal data be modified anyway */

#ifdef PSTD_FILE_NO_CACHE
	if(stat(file->filename, (struct stat*)&file->stat) < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "System call stat returns an unexpected error");
#else
	if(pstd_fcache_stat(file->filename, (struct stat*)&file->stat) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot get the file metadata from file cache");
#endif

	/* dirty hack, but we still need to do this */
	((pstd_file_t*)file)->stat_ready = 1;
#pragma GCC diagnostic pop

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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	/* Because the stdio FILE may modify the file, so we may want to reload the stat again */
	((pstd_file_t*)file)->stat_ready = 0;
#pragma GCC diagnostic pop

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
	_stream_t* stream = (_stream_t*)pstd_mempool_alloc(sizeof(_stream_t));

	if(NULL == stream)
	    ERROR_PTR_RETURN_LOG("Cannot allocate meomry for the stream");

	const pstd_file_t* file = (const pstd_file_t*)mem;
#ifdef PSTD_FILE_NO_CACHE
	stream->file = fopen(file->filename, "rb");
#else
	stream->file = pstd_fcache_open(file->filename);
#endif
	if(NULL == stream->file)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot open file %s", file->filename);

	if(file->partial)
	{
#ifdef PSTD_FILE_NO_CACHE
		if(-1 == fseek(stream->file, (off_t)file->part_beg, SEEK_SET))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot seek file %s", file->filename);
#else
		if(ERROR_CODE(int) == pstd_fcache_seek(stream->file, file->part_beg))
		    ERROR_LOG_GOTO(ERR, "Cannot seek file %s", file->filename);
#endif
		stream->remaining = file->part_size;
	}
	else stream->remaining = (size_t)-1;

	LOG_DEBUG("RLS file %s is opened as a byte stream", file->filename);

	return stream;
ERR:
	if(NULL != stream->file)
	{
#ifdef PSTD_FILE_NO_CACHE
		fclose(stream->file);
#else
		pstd_fcache_close(stream->file);
#endif
	}

	pstd_mempool_free(stream);
	return NULL;
}

/**
 * @brief the close stream callback used by the RLS infrastructure
 * @param stream_mem the stream handle, in this case it should be a stdio FILE
 * @return status code
 **/
static inline int _close(void* stream_mem)
{
	_stream_t* s = (_stream_t*)stream_mem;
#ifdef PSTD_FILE_NO_CACHE
	fclose(s->file);
#else
	if(ERROR_CODE(int) == pstd_fcache_close(s->file))
	    ERROR_RETURN_LOG(int, "Cannot close the file cache reference");
#endif

	LOG_DEBUG("The byte stream interface for RLS file has been closed");
	return pstd_mempool_free(s);
}

/**
 * @brief the end-of-stream callback used by the RLS infrastructure
 * @param stream_mem the stream handle, in this case it should be a stdio FILE
 * return status code or check result
 **/
static inline int _eos(const void* stream_mem)
{
	const _stream_t* s = (const _stream_t*)stream_mem;
	if(s->remaining == 0) return 1;
#ifdef PSTD_FILE_NO_CACHE
	int rc = feof(s->file);
	if(rc < 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot check if the stream gets the end");
	return rc > 0;
#else
	return pstd_fcache_eof(s->file);
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
	_stream_t* s = (_stream_t*)stream_mem;

	if(s->remaining != (size_t)-1 && count > s->remaining)
	    count = s->remaining;

#ifdef PSTD_FILE_NO_CACHE
	size_t rc = fread(buf, 1, count, s->file);
	if(rc == 0 && ferror(s->file))
	    ERROR_RETURN_LOG_ERRNO(size_t, "Cannot read file the RLS file stream");

	LOG_DEBUG("%zu bytes has been read from RLS file byte stream interface", rc);

	if(s->remaining != (size_t)-1)
	    s->remaining -= rc;

	return rc;
#else
	size_t rc = pstd_fcache_read(s->file, buf, count);

	if(ERROR_CODE(size_t) == rc) return ERROR_CODE(size_t);

	if(s->remaining != (size_t)-1)
	    s->remaining -= rc;

	return rc;
#endif
}

/**
 * @brief the callback for generating hash code for the file
 * @param mem the RLS object
 * @param out the output array
 * @return status code
 **/
static inline int _hash(const void* mem, uint64_t out[2])
{
	const pstd_file_t* file = (const pstd_file_t*)mem;
	size_t len = strlen(file->filename);
    /* use different seed for different type of RLS object */
    const uint32_t seed = 93578;
    murmurhash3_128(file->filename, len, seed, out);

    return 0;
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
		.read_func = _read,
        .hash_func = _hash
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
