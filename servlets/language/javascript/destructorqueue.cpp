/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include <algorithm>
#include <vector>
#include <new>

#include <v8engine.hpp>

#include <config.h>

#include <pservlet.h>
#include <pstd.h>

#include <destructorqueue.hpp>


Servlet::DestructorQueue::_node_t::~_node_t()
{
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	v8::HandleScope handle_scope(isolate);
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, *(this->callback));
	if(!func.IsEmpty())
	{
		v8::TryCatch trycatch(isolate);
		v8::Handle<v8::Value> result = func->Call(func, 0, (v8::Handle<v8::Value>*)NULL);
		if(result.IsEmpty())
		{
#if LOG_LEVEL >= ERROR
			v8::Local<v8::Message> message = trycatch.Message();
			v8::String::Utf8Value exception_str(message->Get());
			LOG_ERROR("Uncaught Javascript Exception: %s", *exception_str);
			v8::Local<v8::StackTrace> backtrace = message->GetStackTrace();
			if(!backtrace.IsEmpty())
			{
				int frame_count = backtrace->GetFrameCount(),i;
				for(i = 0; i < frame_count; i ++)
				{
					v8::Local<v8::StackFrame> current_frame = backtrace->GetFrame((uint32_t)i);
					v8::String::Utf8Value current_path(current_frame->GetScriptName());
					v8::String::Utf8Value current_func(current_frame->GetFunctionName());
#ifdef LOG_ERROR_ENABLED
					int line = current_frame->GetLineNumber();
					int col  = current_frame->GetColumn();
					LOG_ERROR("[%d] at %s(%s:%d:%d)",i, *current_func, *current_path, line, col);
#endif
				}
			}
#endif
		}
	}
	this->callback->Reset();
	delete this->callback;
}

Servlet::DestructorQueue::DestructorQueue()
{
	_queue = NULL;
}

Servlet::DestructorQueue::~DestructorQueue()
{
	if(ERROR_CODE(int) == flush())
		LOG_ERROR("Cannot flush the destructor queue");
}

int Servlet::DestructorQueue::add(v8::Persistent<v8::Function>* desc)
{
	_node_t* node = new  _node_t(desc);

	if(NULL == node)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot create new node for the destructor");

	node->next = _queue;

	_queue = node;

	return 0;
}

int Servlet::DestructorQueue::flush()
{
	while(_queue != NULL)
	{
		_node_t* current = _queue;
		_queue = _queue->next;
		delete current;
	}

	return 0;
}
