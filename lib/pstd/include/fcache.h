/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The file cache utilies
 * @file pstd/include/fcache.h
 **/
#ifndef __PSTD_FCACHE_H__
#define __PSTD_FCACHE_H__

/**
 * @brief represent a reference to the entry in the file cache
 **/
typedef struct _pstd_fcache_file_t pstd_fcache_file_t;

/**
 * @brief open a given file from the file cache, if the cache hits, we return the reference to cache entry.
 *        otherwise we will allocate a new cache entry and return a reference to the entry that entry
 * @param filename the file name that we want to access on the disk
 * @return status code
 **/
pstd_fcache_file_t* pstd_fcache_open(const char* filename);

/**
 * @brief dispose a used file cache reference object
 * @param file reference to the file cache entry
 * @return status code
 **/
int pstd_fcache_close(pstd_fcache_file_t* file);

/**
 * @brief read bytes from the file cache reference
 * @param file the reference to the cached file
 * @param buf the data buffer used to return the read result
 * @param bufsize the size of the buffer
 * @return the number of bytes has been read to buffer or error code
 **/
size_t pstd_fcache_read(pstd_fcache_file_t* file, void* buf, size_t bufsize);

/**
 * @brief get the size of the reference to the cached file
 * @param file the file we want to access
 * @return the size of the cached file or error code
 **/
size_t pstd_fcache_size(const pstd_fcache_file_t* file);

/**
 * @brief check if the reference to cached file has reached the end of the file
 * @param file the reference to the cahced file entry to access
 * @return status code
 **/
int pstd_fcache_eof(const pstd_fcache_file_t* file);

/**
 * @brief check if the filename is in the cache
 * @param filename the file name to check
 * @return check result or error code
 **/
int pstd_fcache_is_in_cache(const char* filename);

/**
 * @brief access the file metadata through the cache system, if the file information is prevoiusly cached
 *        just directly return and do not call the disk IO function
 * @param filename the filename to access
 * @param buf the result buffer
 * @return status code
 **/
int pstd_fcache_stat(const char* filename, struct stat* buf);

#endif /*__PSTD_FCACHE_H__ */
