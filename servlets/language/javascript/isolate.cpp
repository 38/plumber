/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <pservlet.h>

#include <error.h>

#include <v8engine.hpp>
#include <isolate.hpp>

using namespace std;

int Servlet::Isolate::init()
{
	if(NULL == (_create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator()))
	    ERROR_RETURN_LOG(int, "Cannot create array buffer allcator");

	if(NULL == (_isolate = v8::Isolate::New(_create_params)))
	    ERROR_RETURN_LOG(int, "Cannot create V8 isolate");

	if(NULL == (_scope = new v8::Isolate::Scope(_isolate)))
	    ERROR_RETURN_LOG(int, "Cannot create V8 isolate scope");

	return 0;
}

Servlet::Isolate::~Isolate()
{
	//_isolate->LowMemoryNotification();

	if(NULL != _scope)
	    delete _scope;

	if(NULL != _isolate)
	    _isolate->Dispose();

	if(NULL != _create_params.array_buffer_allocator)
	    delete _create_params.array_buffer_allocator;
}

v8::Isolate* Servlet::Isolate::get()
{
	return _isolate;
}

