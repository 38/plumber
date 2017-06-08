/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <pservlet.h>

#include <pstd.h>

#include <error.h>

#include <vector>
#include <algorithm>

#include <v8engine.hpp>

#include <blob.hpp>
#include <objectpool.hpp>
#include <destructorqueue.hpp>
#include <context.hpp>
#include <global.hpp>
using namespace std;

Servlet::Global::Global(Servlet::Context* context)
{
	_servlet_context = context;
}

class Creator {
	protected:
	v8::Handle<v8::ObjectTemplate>& _global;
	v8::Isolate* _isolate;
	public:
	Creator(v8::Isolate* isolate, v8::Handle<v8::ObjectTemplate>& global)
	    :_global(global), _isolate(isolate) {}
};

struct FunctionCreator : Creator {
	int operator ()(const char* name, v8::FunctionCallback callback)
	{
		_global->Set(v8::String::NewFromUtf8(_isolate, name), v8::FunctionTemplate::New(_isolate, callback));
		return 0;
	}
	FunctionCreator(v8::Isolate* isolate, v8::Handle<v8::ObjectTemplate>& global)
	    :Creator(isolate, global) {};
};

struct ConstantCreator : Creator {
	int operator ()(const char* name, v8::AccessorGetterCallback callback)
	{
		_global->SetAccessor(v8::String::NewFromUtf8(_isolate, name),  callback, NULL);
		return 0;
	}
	ConstantCreator(v8::Isolate* isolate, v8::Handle<v8::ObjectTemplate>& global)
	    :Creator(isolate, global) {};
};

int Servlet::Global::init()
{
	if(_servlet_context == NULL)
	    ERROR_RETURN_LOG(int, "Invalid servlet context");

	v8::Isolate* isolate = Servlet::Context::get_isolate();
	if(NULL == isolate)
	    ERROR_RETURN_LOG(int, "Cannot get isolate");

	v8::HandleScope handle_scope(isolate);
	v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

	if(ERROR_CODE(int) == _servlet_context->for_each_function(FunctionCreator(isolate, global)))
	    ERROR_RETURN_LOG(int, "Cannot register functions");

	if(ERROR_CODE(int) == _servlet_context->for_each_const(ConstantCreator(isolate, global)))
	    ERROR_RETURN_LOG(int, "Cannot register constants");

	v8::Handle<v8::Context> context = v8::Context::New(isolate, NULL, global);

	_v8_context.Reset(isolate, context);

	return 0;
}

v8::Persistent<v8::Context>& Servlet::Global::get()
{
	return _v8_context;
}

