/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <utils/static_assertion.h>

#include <error.h>

#include <pstd.h>
#include <pstd/types/string.h>

/**
 * @brief the actuall data structure for the PSTD string type
 **/
struct _pstd_string_t {
	char*  buffer;            /*!< the actual buffer that uses this */
	size_t capacity;          /*!< the capacity of the string buffer */
	size_t length;            /*!< the length of the string */
	uint32_t commited:1;      /*!< if this string has been commited */
	uintpad_t __padding__;
	union {
		char     _def_buf[128];   /*!< the default initial buffer */
		const char* immutable;    /*!< The pointer for the immutable string */
	};
};
STATIC_ASSERTION_LAST(pstd_string_t, _def_buf);

/**
 * @brief represent a string stream state
 **/
typedef struct {
	const pstd_string_t* string;   /*!< the string object */
	size_t               location; /*!< the current location */
} _stream_t;

pstd_string_t* pstd_string_from_onwership_pointer(char* data, size_t sz)
{
	if(NULL == data) ERROR_PTR_RETURN_LOG("Invalid arguments");
	pstd_string_t* ret = pstd_string_new(0);

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate string object");

	ret->buffer = data;
	ret->capacity = sz;
	ret->length = sz;
	ret->commited = 0;

	return ret;
}

pstd_string_t* pstd_string_new_immutable(const char* data, size_t sz)
{
	if(NULL == data || sz == ERROR_CODE(size_t))
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	pstd_string_t* ret = pstd_string_new(0);
	
	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate string object");

	ret->buffer   = NULL;
	ret->capacity = 0;
	ret->length   = sz;
	ret->commited = 0;
	ret->immutable = data;

	return ret;
}

pstd_string_t* pstd_string_new(size_t initcap)
{
	/* Of course, even if the initcap larger than 128, the default buffer is a waste of memory
	 * But most of the string is smaller than 128 bytes, and this also makes the object fixed size
	 * which is good for memory allocation performance */
	pstd_string_t* ret = pstd_mempool_alloc(sizeof(*ret));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate string object");

	if(initcap <= sizeof(ret->_def_buf))
	{
		LOG_DEBUG("The required buffer size is smaller than 128 bytes, use the pooled memory");
		ret->capacity = sizeof(ret->_def_buf);
		ret->buffer = ret->_def_buf;
	}
	else
	{
		LOG_DEBUG("The required buffer size is larger than 128 bytes, no pooled memory avaliable");
		ret->capacity = initcap;
		if(NULL == (ret->buffer = malloc(ret->capacity)))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate buffer for the string object");
	}
	ret->length = 0;
	ret->commited = 0;
	return ret;
ERR:
	if(NULL != ret) pstd_mempool_free(ret);
	return NULL;
}
/**
 * @brief the implementation for disposing a string buffer
 * @param str the str to dispose
 * @param user_space_call if this function is called from user-space, if
 *        this is true, then we do not allow it dispose the commited string
 * @return status code
 **/
static inline int _free_impl(pstd_string_t* str, int user_space_call)
{
	int rc = 0;
	if(NULL == str)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(user_space_call && str->commited)
	    ERROR_RETURN_LOG(int, "Cannot dispose a committed string from user-space");

	if(NULL != str->buffer)
	{
		if(str->buffer != str->_def_buf)
		    free(str->buffer);
	}

	if(ERROR_CODE(int) == pstd_mempool_free(str))
	    rc = ERROR_CODE(int);

	return rc;

}
int pstd_string_free(pstd_string_t* str)
{
	return _free_impl(str, 1);
}

const pstd_string_t* pstd_string_from_rls(scope_token_t token)
{
	return (const pstd_string_t*)pstd_scope_get(token);
}

const char* pstd_string_value(const pstd_string_t* str)
{
	if(NULL == str) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return str->buffer == NULL ? str->immutable : str->buffer;
}

size_t pstd_string_length(const pstd_string_t* str)
{
	if(NULL == str) ERROR_RETURN_LOG(size_t, "Invalid arguments");

	return str->length;
}

pstd_string_t* pstd_string_copy_rls(scope_token_t token, scope_token_t* token_buf)
{
	if(ERROR_CODE(uint32_t) == token || NULL == token_buf)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	pstd_string_t* ret;
	if(ERROR_CODE(scope_token_t) == (*token_buf = pstd_scope_copy(token, (void**)&ret)))
	    ERROR_PTR_RETURN_LOG("Cannot duplicate the token %u", token);

	return ret;
}

/**
 * @brief the function used as the callback fucntion which will be invoked when the RLS infrastructure
 *        wants to dispose the memory
 * @param mem the memory to dispose
 * @return status code
 **/
static inline int _free(void* mem)
{
	LOG_DEBUG("RLS string disposed");
	return _free_impl((pstd_string_t*)mem, 0);
}

/**
 * @brief the function used as the callback function which will be invoked when the RLS infrasturcture
 *        needs to copy the RLS entity
 * @param mem the memory to copy
 * @return the poiner to copied memory, NULL on error
 **/
static inline void* _copy(const void* mem)
{
	if(NULL == mem)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const pstd_string_t* ptr = (const pstd_string_t*)mem;

	LOG_DEBUG("RLS string duplicated");
	pstd_string_t* ret = pstd_string_new(ptr->buffer != NULL ? ptr->length + 1 : 0);

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot create new string object for the duplication");

	if(ret->buffer != NULL)
		memcpy(ret->buffer, ptr->buffer, ptr->length + 1);
	else
		ret->immutable = ptr->immutable;

	ret->length = ptr->length;
	ret->commited = 1;   /*!< it's commited by default */

	return ret;
}

/**
 * @brief open a RLS string as stream
 * @param mem the RLS string
 * @return the stream object
 **/
static inline void* _open(const void* mem)
{
	if(NULL == mem)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const pstd_string_t* str = (const pstd_string_t*)mem;

	LOG_DEBUG("Open the RLS string as byte stream");

	_stream_t* ret = (_stream_t*)pstd_mempool_alloc(sizeof(*ret));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot allocate memory for the new stream object");

	ret->string = str;
	ret->location = 0;

	return ret;
}

/**
 * @brief close a RLS string stream
 * @param stream_mem the memory for the stream state
 * @return status code
 **/
static inline int _close(void* stream_mem)
{
	if(NULL == stream_mem)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	LOG_DEBUG("Closing the RLS string stream");

	return pstd_mempool_free(stream_mem);
}

/**
 * @brief check if current stream has reached the end
 * @param stream_mem the memory for the stream handle
 * @return the check result or error code
 **/
static inline int _eos(const void* stream_mem)
{
	if(NULL == stream_mem)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _stream_t* stream = (const _stream_t*)stream_mem;

	return stream->location >= stream->string->length;
}

/**
 * @brief read the string stream to buffer
 * @param stream_mem the stream handle
 * @param buf the buffer to return result
 * @param count the number of bytes want to read
 * @return the number of bytes actually read
 **/
static inline size_t _read(void* __restrict stream_mem, void* __restrict buf, size_t count)
{
	if(NULL == stream_mem || NULL == buf)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	_stream_t* stream = (_stream_t*)stream_mem;

	size_t bytes_can_read = count;
	if(bytes_can_read + stream->location > stream->string->length)
	    bytes_can_read = stream->string->length - stream->location;

	if(stream->string->buffer != NULL)
		memcpy(buf, stream->string->buffer + stream->location, bytes_can_read);
	else
		memcpy(buf, stream->string->immutable + stream->location, bytes_can_read);

	stream->location += bytes_can_read;
	return bytes_can_read;
}

scope_token_t pstd_string_commit(pstd_string_t* str)
{
	if(NULL == str)
	    ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");
	if(str->commited)
	    ERROR_RETURN_LOG(scope_token_t, "The string has been commited prevoiusly");

	str->commited = 1;
	str->buffer[str->length] = 0;

	scope_entity_t ent = {
		.data = str,
		.copy_func = _copy,
		.free_func = _free,
		.open_func = _open,
		.close_func = _close,
		.read_func = _read,
		.eos_func = _eos
	};

	return pstd_scope_add(&ent);
}
/**
 * @brief ensure the capacity of the string buffer has at least required_size bytes unused memory
 * @param str the string buffer
 * @param required_size the number of bytes required
 * @return status code
 **/
static inline int _ensure_capacity(pstd_string_t* str, size_t required_size)
{
	size_t newcap = str->capacity;
	for(;newcap < str->length + required_size + 1; newcap *= 2);
	if(newcap == str->capacity) return 0;

	char* newbuf = NULL;
	if(str->_def_buf == str->buffer)
	{
		if(NULL == (newbuf = (char*)malloc(newcap)))
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate heap memory for the larger buffer: size %zu", newcap);
		memcpy(newbuf, str->buffer, str->length + 1);
	}
	else
	{
		if(NULL == (newbuf = (char*)realloc(str->buffer, newcap)))
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the heap memory buffer to size %zu", newcap);
	}

	str->capacity = newcap;
	str->buffer   = newbuf;

	return 0;
}

size_t pstd_string_write(pstd_string_t* str, const char* data, size_t size)
{
	if(NULL == str || NULL == data || size == ERROR_CODE(size_t) || str->buffer == NULL)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	if(size == 0) return 0;

	if(ERROR_CODE(int) == _ensure_capacity(str, size))
	    ERROR_RETURN_LOG(size_t, "Cannot ensure the string buffer have enough space");

	memcpy(str->buffer + str->length, data, size);

	str->length += size;

	return size;
}

size_t pstd_string_vprintf(pstd_string_t* str, const char* fmt, va_list ap)
{
	if(NULL == str || NULL == fmt || str->buffer == NULL)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");
	size_t ret = 0;

	for(;;)
	{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
		int rc = vsnprintf(str->buffer + str->length, str->capacity - str->length, fmt, ap);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
		if(rc < 0)
		    ERROR_RETURN_LOG(size_t, "Cannot output the formated string to the string buffer");

		ret = (size_t)rc;

		if(ret + 1 <= str->capacity - str->length) break;

		if(ERROR_CODE(int) == _ensure_capacity(str, str->capacity - str->length))
		    ERROR_RETURN_LOG(size_t, "Cannot resize the buffer to fit the buffer size");
	}
	str->length += ret;
	return ret;
}

size_t pstd_string_printf(pstd_string_t* str, const char* fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	size_t ret = pstd_string_vprintf(str, fmt, ap);
	va_end(ap);
	return ret;
}
