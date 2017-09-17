/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the RLS type for a file on the filesystem
 * @details This is the impelmenetation for the RLS file, which is used to represent a
 *         reference of a file on the filesystem. By having the file RLS, we will be able
 *         to avoid passing the file content through the pipe (especially for the large files)
 *         Also, the file RLS can be cached, so that we can avoid disk IO in this way
 * @file pstd/include/pstd/types/file.h
 **/
#ifndef __PSTD_TYPE_FILE_H__
#define __PSTD_TYPE_FILE_H__

#	ifdef __cplusplus
extern "C" {
#	endif /* __cplusplus__ */

	/**
	* @brief Represent a reference to a file on the file system
	**/
	typedef struct _pstd_file_t pstd_file_t;

	/**
	* @brief create a reference to the given filename
	* @param filename the file name we want to refer
	* @return the newly created RLS file object
	**/
	pstd_file_t* pstd_file_new(const char* filename);

	/**
	* @brief dispose a used RLS file object
	* @param file the used file object to dispose
	* @return status code
	**/
	int pstd_file_free(pstd_file_t* file);

	/**
	* @brief get a RLS file object from the request local scope
	* @param token the token to get
	* @return the object from the RLS or NULL on error case
	**/
	const pstd_file_t* pstd_file_from_rls(scope_token_t token);

	/* This object do not support copy, because it should be immutable */

	/**
	* @brief check if the file is exist
	* @param file the file to check
	* @return the check result, 1 for yes, 0 for no, or error code for error cases
	**/
	int pstd_file_exist(const pstd_file_t* file);

	/**
	* @brief get the size of the file
	* @note this assume the file is already on the disk
	* @param file the file to check
	* @return the size of the file, or error code
	**/
	size_t pstd_file_size(const pstd_file_t* file);

	/**
	* @brief open the stdio file for the given RLS file object
	* @param file the RLS file
	* @param mode the mode
	* @return the stdio file object, or error code
	* @note the caller should close the file after use
	**/
	FILE* pstd_file_open(const pstd_file_t* file, const char* mode);

	/**
	* @brief commit the given RLS file object to the request local scope
	* @param file the file RLS to commit
	* @note if the call succeeded, the ownership of file will be transferred
	* @return the token in the scope for this object, or error code
	**/
	scope_token_t pstd_file_commit(pstd_file_t* file);

	/**
	* @brief Get the filename for this PSTD file
	* @param file The filename
	* @return The file name or NULL on error
	**/
	const char* pstd_file_name(const pstd_file_t* file);

#	ifdef __cplusplus
}
#	endif /* __cplusplus__ */

#endif /* __PSTD_TYPE_FILE_H__ */
