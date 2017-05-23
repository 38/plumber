/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <error.h>
#include <pservlet.h>

#include <blob.hpp>

Servlet::Blob::Blob()
{
	_capacity = 0;
	_size = 0;
	_data = NULL;
}

Servlet::Blob::~Blob()
{
	if(NULL != _data) free(_data);
}

int Servlet::Blob::init(size_t capacity)
{
	if(capacity == 0) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(NULL == _data)
	{
		if(NULL == (_data = (char*)malloc(capacity)))
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the bolb buffer in size %zu", capacity);

		_size = 0;
		_capacity = capacity;

	}

	return 0;
}

char& Servlet::Blob::operator [](size_t idx)
{
	return _data[idx];
}

Servlet::Blob::operator const char*() const
{
	return _data;
}

size_t Servlet::Blob::size() const
{
	if(NULL == _data) ERROR_RETURN_LOG(size_t, "Uninitialized blob");
	return _size;
}

int Servlet::Blob::append(const char* data, size_t count)
{
	if(NULL == data || count == 0) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == ensure_space(count))
	    ERROR_RETURN_LOG(int, "Cannot ensure buffer size");

	memcpy(_data, data, count);
	_size += count;

	return 0;
}

int Servlet::Blob::append(size_t count)
{
	if(count == 0) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(NULL == _data) ERROR_RETURN_LOG(int, "Blob buffer is not initialized");

	if(count + _size > _capacity) _size = _capacity;
	else _size += count;

	return 0;
}

int Servlet::Blob::ensure_space(size_t count)
{
	if(NULL == _data) ERROR_RETURN_LOG(int, "Blob buffer is not initialized");

	if(_size + count > _capacity)
	{
		size_t new_cap = _capacity;
		while(new_cap < _size + count)
		    new_cap *= 2;
		LOG_DEBUG("Resizing the blob buffer from size %zu to %zu", _capacity, new_cap);

		char* _new_buffer = (char*)realloc(_data, new_cap);
		if(NULL == _new_buffer)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the blob buffer");

		_data = _new_buffer;
		_capacity = new_cap;
	}

	return 0;
}

size_t Servlet::Blob::space_available_without_resize()
{
	return _capacity - _size;
}
