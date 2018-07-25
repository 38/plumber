/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include <pservlet.h>

#include <error.h>

#include <blob.hpp>
#include <objectpool.hpp>


int Servlet::ObjectPool::Pool::_resize()
{
	uint32_t _next_size = _capacity * 2;

	_Pointer* next_array = (_Pointer*)realloc(_pointers, _next_size * sizeof(_Pointer));

	if(NULL == next_array)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot resize memory for the pointer array");

	for(uint32_t i = _capacity; i < _next_size - 1; i ++)
	{
		next_array[i].next_unused = i + 1;
		next_array[i].typecode = -1;
	}

	next_array[_next_size - 1].next_unused = _first_unused;
	_first_unused = _capacity;
	_capacity = _next_size;
	_pointers = next_array;

	return 0;
}

Servlet::ObjectPool::Pool::Pool()
{
	_first_unused = 0;
	_capacity = 1;
	_pointers = (_Pointer*)malloc(sizeof(_Pointer));
	if(NULL == _pointers)
	{
		LOG_ERROR_ERRNO("Canont allocate memory for the pointer array");
		return;
	}

	_pointers->next_unused = ERROR_CODE(uint32_t);
	_pointers->typecode = -1;
}

Servlet::ObjectPool::Pool::~Pool()
{
	if(NULL != _pointers)
	{
		for(uint32_t i = _first_unused; i != ERROR_CODE(uint32_t); )
		{
			uint32_t next = _pointers[i].next_unused;
			_pointers[i].ptr = NULL;
			i = next;
		}

		for(uint32_t i = 0; i < _capacity; i ++)
		{
			if(NULL == _pointers[i].ptr) continue;
			int ret = Destructor<>::dispose(_pointers[i].typecode, _pointers[i].ptr);
			if(ERROR_CODE(int) == ret)
				LOG_WARNING("Cannot dipose object at %u, leaking memory", i);
		}

		free(_pointers);
	}
}

bool Servlet::ObjectPool::Pool::check_initialized()
{
	return NULL != _pointers;
}

int Servlet::ObjectPool::Pool::dispose_object(uint32_t id)
{

	int ret = Destructor<>::dispose(_pointers[id].typecode, _pointers[id].ptr);
	if(ERROR_CODE(int) == ret)
		LOG_WARNING("Cannot dipsoe object #%u, leaking memory", id);

	_pointers[id].next_unused = _first_unused;
	_pointers[id].typecode = -1;
	_first_unused = id;

	return ret;
}



