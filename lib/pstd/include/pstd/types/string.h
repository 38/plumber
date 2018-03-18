/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the standard string buffer request local scope data implementation
 * @details This is the implementaiton for the string buffer data type for the standard
 *          RLS string, which we can used to pass a reference of string via the pipe,
 *          to avoid copying the entire string again and again. <br/>
 *          When a string buffer is created, the servlet can register it to the RLS and
 *          get a RLS token for the string, then the RLS token can be written to the pipe,
 *          so that the downstream can get the token and use the API to get the data from
 *          the RLS
 * @todo    This implementaiton can not across the machine boarder (Because it uses the customized
 *          callback functions) We need to make this a built-in RLS type once we start working on
 *          the strong-typed pipes
 * @file    pstd/include/pstd/types/string.h
 **/
#ifndef __PSTD_TYPES_STRING_H__
#define __PSTD_TYPES_STRING_H__

#include <string.h>
#include <stdlib.h>

#	ifdef __cplusplus
extern "C" {
#	endif /* __cplusplus__ */

	/**
	* @brief the string buffer type
	**/
	typedef struct _pstd_string_t pstd_string_t;

	/**
	 * @brief Create a new pstd string from a ownership pointer
	 * @note This is used when the data pointer is created externally and we want the RLS
	 *       take over the owership of this  pointer
	 * @param data The data pointer
	 * @param sz The size of the data section
	 * @return newly create PSTD String object, NULL on error case
	 **/
	pstd_string_t* pstd_string_from_onwership_pointer(char* data, size_t sz);

	/**
	 * @brief Create a new string from a onwership pointer, similar to pstd_string_from_onwership_pointer
	 *        but this function only expose data from data + begin to data + end.
	 * @note This is useful when we have a buffer which is owned by us, however, what we want the next servlet
	 *       to get is just a part of the string. Thus we are use this function to avoid copy
	 * @param data The data buffer
	 * @param begin The offset where the data section begins
	 * @param end The offset where the data section ends
	 * @return the newly created string object
	 **/
	pstd_string_t* pstd_string_from_onwership_pointer_range(char* data, size_t begin, size_t end);

	/**
	 * @brief Create a new immutable string from the ownership pointer
	 * @param data The imuutable string
	 * @param sz The size
	 * @return newly created string object, NULL on error
	 **/
	pstd_string_t* pstd_string_new_immutable(const char* data, size_t sz);

	/**
	 * @brief Create a new immutable string from a constant string pointer
	 * @param data The immutable string
	 * @return newly created string object
	 **/
	static inline pstd_string_t* pstd_string_from_const(const char* data)
	{
		if(NULL == data) ERROR_PTR_RETURN_LOG("Invalid arguments");

		size_t sz = strlen(data);

		return pstd_string_new_immutable(data, sz);
	}

	/**
	* @brief create a new pstd string buffer
	* @param initcap the initial capacity of the string bufer
	* @return the newly created pstd string buffer, NULL on error case
	**/
	pstd_string_t* pstd_string_new(size_t initcap);

	/**
	* @brief dispose a used string buffer
	* @param str the used string buffer
	* @return status code
	**/
	int pstd_string_free(pstd_string_t* str);

	/**
	* @brief get a shared string buffer from the RLS
	* @param token the RLS token
	* @return the target string buffer, NULL on error case
	**/
	const pstd_string_t* pstd_string_from_rls(scope_token_t token);

	/**
	* @brief get the underlying string from the string buffer
	* @param str the string buffer
	* @return the underlying string, error NULL on error
	**/
	const char* pstd_string_value(const pstd_string_t* str);

	/**
	* @brief get the number of bytes has been written to the string buffer
	* @param str the string buffer
	* @return the size of the string buffer or error code
	**/
	size_t pstd_string_length(const pstd_string_t* str);

	/**
	* @brief make a writable copy of the given token
	* @param token the token to copy
	* @param token_buf the buffer used to return the token to the writable copy
	* @return the pointer to the writable copy
	* @note this is the function that implmenets the copy-on-write functionality
	**/
	pstd_string_t* pstd_string_copy_rls(scope_token_t token, scope_token_t* token_buf);

	/**
	* @brief commit a string buffer to the RLS
	* @param str the string buffer to commit
	* @return the RLS token for the buffer or error code
	**/
	scope_token_t pstd_string_commit(pstd_string_t* str);

	/**
	* @brief write the bytes to the buffer
	* @param str the string buffer
	* @param data the data pointer to write
	* @param size the size of the data section
	* @return the number of bytes has been written, NULL on error case
	**/
	size_t pstd_string_write(pstd_string_t* str, const char* data, size_t size);

	/**
	* @brief print the formated information to the string
	* @param str the string buffer
	* @param fmt the format string
	* @return the number of bytes has been written, NULL on error case
	**/
	size_t pstd_string_printf(pstd_string_t* str, const char* fmt, ...)
	    __attribute__((format (printf, 2, 3)));

	/**
	* @brief append the formatted string to the string object. This is similar to pstd_string_printf, but it accepts
	*        va_list as additional parameters
	* @param str The string object
	* @param fmt The formatting string
	* @param ap  The additional parameter
	* @return The number of bytes has been appended to the string object
	**/
	size_t pstd_string_vprintf(pstd_string_t* str, const char* fmt, va_list ap);

	/**
	 * @brief Create and commit a string constant
	 * @param str The string to commit
	 * @return the Scope token we have commited
	 **/
	static inline scope_token_t pstd_string_create_commit(const char* str)
	{
		pstd_string_t* str_rls_obj = pstd_string_from_const(str);

		if(NULL == str_rls_obj)
		    ERROR_RETURN_LOG(scope_token_t, "Cannot create the PSTD string object");

		scope_token_t str_rls_tok = pstd_string_commit(str_rls_obj);
		if(ERROR_CODE(scope_token_t) == str_rls_tok)
		{
			pstd_string_free(str_rls_obj);
			ERROR_RETURN_LOG(scope_token_t, "Cannot commit the RLS string to the scope");
		}

		return str_rls_tok;
	}

	/**
	 * @brief Create, commit and write an immutable RLS string to the typed pipe
	 * @note This function is almost identical to pstd_string_create_commit_write, the only difference is instead of
	 *       detect the string length, it use the given size specified by caller
	 * @param type_inst The type instance
	 * @param accessor The field accessor
	 * @param str The string to commit
	 * @param len The size of the string to commit
	 * @return status code
	 **/
	static inline int pstd_string_create_commit_write_sz(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor, const char* str, size_t len)
	{
		pstd_string_t* str_rls_obj = pstd_string_new_immutable(str, len);

		if(NULL == str_rls_obj)
		    ERROR_RETURN_LOG(int, "Cannot create the PSTD string object");

		scope_token_t str_rls_tok = pstd_string_commit(str_rls_obj);
		if(ERROR_CODE(scope_token_t) == str_rls_tok)
		{
			pstd_string_free(str_rls_obj);
			ERROR_RETURN_LOG(int, "Cannot commit the RLS string to the scope");
		}

		return PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, accessor, str_rls_tok);

	}

	/**
	 * @brief Create, commit and write an immutable RLS string object to the typed pipe
	 * @note This is the helper function to write a string constant to the scope
	 * @param type_inst The type instance we used for writing
	 * @param accessor The field accessor we want to write
	 * @param str The string we want to write
	 * @return status code
	 **/
	static inline int pstd_string_create_commit_write(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor, const char* str)
	{
		if(NULL == str)
		    ERROR_RETURN_LOG(int, "Invalid arguments");
		return pstd_string_create_commit_write_sz(type_inst, accessor, str, strlen(str));
	}

	/**
	 * @brief Transfer the ownership of the pointer, commit and write a mutable RLS string
	 * @note This is the helper function to write a string to typed header. Unlike other version of pstd_string_*_commit*, it
	 *       takes the ownership of the string
	 * @param type_inst The type instance
	 * @param accessor The accessor
	 * @param str The string to commit
	 * @param size The size of the string buffer
	 * @return status code
	 **/
	static inline int pstd_string_transfer_commit_write(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor, char* str, size_t size)
	{
		pstd_string_t* str_rls_obj = pstd_string_from_onwership_pointer(str, size);

		if(NULL == str_rls_obj)
		    ERROR_RETURN_LOG(int, "Cannot create PSTD string object");

		scope_token_t str_rls_tok = pstd_string_commit(str_rls_obj);
		if(ERROR_CODE(scope_token_t) == str_rls_tok)
		{
			pstd_string_free(str_rls_obj);
			ERROR_RETURN_LOG(int, "Cannot commit the RLS string object to the scope");
		}

		return PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, accessor, str_rls_tok);
	}

	/**
	 * @brief Transfer the ownership of the pointer and commit, wrie a mutable RLS string
	 * @note This is different from pstd_string_transfer_commit_write. It only expose a range of data from the buffer.
	 *       But when the object is disposed it will dispose the entire buffer
	 * @param type_inst The type instance
	 * @param accessor The accessor
	 * @param str The string to commit
	 * @param begin The begin offset
	 * @param end The end offset
	 * @return status code
	 **/
	static inline int pstd_string_transfer_commit_write_range(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor, char* str, size_t begin, size_t end)
	{
		pstd_string_t* str_rls_obj = pstd_string_from_onwership_pointer_range(str, begin, end);

		if(NULL == str_rls_obj)
		    ERROR_RETURN_LOG(int, "Cannot create PSTD string object");

		scope_token_t str_rls_tok = pstd_string_commit(str_rls_obj);
		if(ERROR_CODE(scope_token_t) == str_rls_tok)
		{
			pstd_string_free(str_rls_obj);
			ERROR_RETURN_LOG(int, "Cannot commit the RLS string object to the scope");
		}

		return PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, accessor, str_rls_tok);
	}

	/**
	 * @brief Copy, commit and write a mutable RLS string object to the typed pipe
	 * @note This is the helper function to write a string to typed header. Unlike the create version, this function copy the string.
	 *       This function unlike the one without _sz suffix, it actually uses the caller specified size
	 * @param type_inst The type instance we used for writing
	 * @param accessor The field accessor we want to write
	 * @param str The string we want to write
	 * @param sz The size of the string to write
	 * @return status code
	 **/
	static inline int pstd_string_copy_commit_write_sz(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor, const char* str, size_t sz)
	{
		if(NULL == str)
		    ERROR_RETURN_LOG(int, "Invalid arguments");

		char* owned_str = (char*)malloc(sz + 1);

		if(NULL == owned_str)
		    ERROR_RETURN_LOG(int, "Cannot allocate memory for the new string");

		memcpy(owned_str, str, sz);
		owned_str[sz] = 0;

		pstd_string_t* str_rls_obj = pstd_string_from_onwership_pointer(owned_str, sz);

		if(NULL == str_rls_obj)
		{
			free(owned_str);
			ERROR_RETURN_LOG(int, "Cannot create the PSTD string object");
		}

		scope_token_t str_rls_tok = pstd_string_commit(str_rls_obj);
		if(ERROR_CODE(scope_token_t) == str_rls_tok)
		{
			pstd_string_free(str_rls_obj);
			ERROR_RETURN_LOG(int, "Cannot commit the RLS string to the scope");
		}

		return PSTD_TYPE_INST_WRITE_PRIMITIVE(type_inst, accessor, str_rls_tok);
	}

	/**
	 * @brief Copy, commit and write a mutable RLS string object to the typed pipe
	 * @note This is the helper function to write a string to typed header. Unlike the create version, this function copy the string.
	 * @param type_inst The type instance we used for writing
	 * @param accessor The field accessor we want to write
	 * @param str The string we want to write
	 * @return status code
	 **/
	static inline int pstd_string_copy_commit_write(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor, const char* str)
	{
		if(NULL == str)
		    ERROR_RETURN_LOG(int, "Invalid arguments");
		return pstd_string_copy_commit_write_sz(type_inst, accessor, str, strlen(str));
	}

	/**
	 * @brief Retrieve a string value using the accessor
	 * @param type_inst The type instance
	 * @param accessor The accessor we should use
	 * @param defval The default value
	 * @return The string value
	 **/
	static inline const char* pstd_string_get_data_from_accessor(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor, const char* defval)
	{
		scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, type_inst, accessor);
		if(ERROR_CODE(scope_token_t) == token)
		    ERROR_PTR_RETURN_LOG("Cannot read the RLS token value with given accessor");

		if(0 == token) return defval;

		const pstd_string_t* str_obj = pstd_string_from_rls(token);

		if(NULL == str_obj)
		    ERROR_PTR_RETURN_LOG("Cannot retrieve the string object from RLS");

		return pstd_string_value(str_obj);
	}

	/**
	 * @brief Retrive a string object with the type accessor
	 * @param type_inst The type instance
	 * @Param accessor The accessor to the string token
	 * @return The string object
	 **/
	static inline const pstd_string_t* pstd_string_from_accessor(pstd_type_instance_t* type_inst, pstd_type_accessor_t accessor)
	{
		scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, type_inst, accessor);
		if(ERROR_CODE(scope_token_t) == token)
		    ERROR_PTR_RETURN_LOG("Cannot read the RLS token value with given accessor");

		if(token == 0) return NULL;

		return pstd_string_from_rls(token);
	}

#	ifdef __cplusplus
}
#	endif /* __cplusplus__ */

#endif /* __PSTD_TYPES_STRING_H__ */
