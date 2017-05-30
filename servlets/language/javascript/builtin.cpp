/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <error.h>

#include <pservlet.h>
#include <pstd.h>

#include <v8engine.hpp>

#include <blob.hpp>
#include <objectpool.hpp>
#include <context.hpp>
#include <builtin.hpp>

#define _JSFUNCTION(name) \
    static inline void _##name(const v8::FunctionCallbackInfo<v8::Value>& args) \

#define _JSFUNCTION_INIT \
        v8::Isolate* isolate = v8::Isolate::GetCurrent();\
        if(NULL == isolate) return;

#define _JS_THROW(type, msg) \
        do {\
	        isolate->ThrowException(v8::Exception::type(v8::String::NewFromUtf8(isolate, msg)));\
	        return;\
        }while(0)


#define _CHECK_ARGC(N) \
    do{\
	    if(args.Length() != (N)) _JS_THROW(Error, "Wrong number of arguments");\
    } while(0)

#define _CHECK_TYPE(k, type) \
    do {\
	    if(args[(k)].IsEmpty() || !args[(k)]->Is##type()) _JS_THROW(TypeError, #type" expected");\
    } while(0)

#define _READ_U32(name, k) \
    _CHECK_TYPE(k, Uint32); \
    uint32_t name = args[(k)]->Uint32Value()

#define _READ_I32(name, k) \
    _CHECK_TYPE(k, Int32); \
    int32_t name = args[(k)]->Int32Value()

#define _READ_STR(name, k) \
    v8::String::Utf8Value __##name##_object(args[(k)]);\
    const char* name = *__##name##_object

#define _READ_FUNC(name, k) \
    v8::Local<v8::Function> name = v8::Local<v8::Function>::Cast(args[(k)]);\
    if(name.IsEmpty()) _JS_THROW(TypeError, "Function expected");

#define _READ_BOOL(name, k) \
    _CHECK_TYPE(k, Boolean); \
    bool name = args[(k)]->BooleanValue();

_JSFUNCTION(log)
{
	_JSFUNCTION_INIT;

	_CHECK_ARGC(2);

	_READ_U32(level, 0);
	_READ_STR(message, 1);

#define __LOG_LEVEL__(l) else if(l == level) { LOG_##l("%s", message); }
	if(0);
	__LOG_LEVEL__(FATAL)
	__LOG_LEVEL__(ERROR)
	__LOG_LEVEL__(WARNING)
	__LOG_LEVEL__(NOTICE)
	__LOG_LEVEL__(INFO)
	__LOG_LEVEL__(TRACE)
	__LOG_LEVEL__(DEBUG)
#undef __LOG_LEVEL__
}

_JSFUNCTION(define)
{
	_JSFUNCTION_INIT;

	_CHECK_ARGC(3);

	_READ_STR(name, 0);
	_READ_I32(flags, 1);

	if(!args[2]->IsUndefined())
	{
		_READ_STR(type_expr, 2);
		args.GetReturnValue().Set((int32_t)pipe_define(name, (pipe_flags_t)flags, type_expr));
	}
	else
		args.GetReturnValue().Set((int32_t)pipe_define(name, (pipe_flags_t)flags, NULL));
}

_JSFUNCTION(import)
{
	_JSFUNCTION_INIT;

	if(args.Length() <= 0 || args.Length() > 3)
	    _JS_THROW(Error, "Invalid arguments");

	_READ_STR(filename, 0);
	char* script = NULL;

	if(args.Length() == 1)
	    script = Servlet::Context::load_script_from_file(filename);
	else if(args.Length() == 2)
	{
		_READ_STR(header, 1);
		script = Servlet::Context::load_script_from_file(filename, header);
	}
	else if(args.Length() == 3)
	{
		_READ_STR(header, 1);
		_READ_STR(trailer, 2);
		script = Servlet::Context::load_script_from_file(filename, header, trailer);
	}
	else
	    _JS_THROW(Error, "Wrong # of arguments");

	if(NULL == script) _JS_THROW(Error, "Cannot load script file");

	int rc = Servlet::Context::import_script(isolate, script, filename);

	if(ERROR_CODE(int) == rc)
	    return;
}

_JSFUNCTION(read)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(2);
	_READ_I32(pipe_s, 0);
	_READ_I32(count, 1);

	pipe_t pipe = (pipe_t)pipe_s;

	Servlet::ObjectPool::Pool* pool = Servlet::Context::get_object_pool();
	if(NULL == pool) _JS_THROW(Error, "Internal Error");

	size_t howmany = count > 0 ? (size_t)count : (size_t)-1;

	Servlet::ObjectPool::Pool::Pointer<Servlet::Blob> blob = pool->create<Servlet::Blob>();
	if(blob.is_null()) _JS_THROW(Error, "Interal Error: Cannot create object");

	if(blob->init(howmany == (size_t)-1 ? 4096 : howmany) == ERROR_CODE(int))
	    _JS_THROW(Error, "Interal Error: Cannot initialize the blob buffer");

	size_t offset = 0;

	for(;howmany > 0 || howmany == (size_t)-1;)
	{
		size_t bytes_to_read = howmany != (size_t)-1 ? howmany : blob->space_available_without_resize();

		if(bytes_to_read == 0) bytes_to_read = blob->size();
		if(bytes_to_read > howmany) bytes_to_read = howmany;

		if(ERROR_CODE(int) == blob->ensure_space(bytes_to_read))
		    _JS_THROW(Error, "Internal Error: Error while resizing the blob buffer");

		char* buffer = &(*blob)[offset];

		size_t bytes_read = pipe_read((pipe_t)pipe, buffer, bytes_to_read);
		if(bytes_read == 0) break;
		if(bytes_read == ERROR_CODE(size_t))
		    _JS_THROW(Error, "pipe read error");

		if(ERROR_CODE(int) == blob->append(bytes_read))
		    _JS_THROW(Error, "Internal error: Error while writing blob buffer");

		if(bytes_read == 0) break;

		offset += bytes_read;
		if(howmany != (size_t)-1) howmany -= bytes_read;
	}

	blob.preserve();

	args.GetReturnValue().Set((int32_t)blob);
}
_JSFUNCTION(gc)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(0);
	isolate->LowMemoryNotification();
}

struct _SentinelData {
	static void _on_destory(const v8::WeakCallbackInfo<_SentinelData>& info)
	{
		auto param = info.GetParameter();
		delete param;
	}
	v8::Persistent<v8::Object>    sentinel;
	v8::Persistent<v8::Function>  callback;
	_SentinelData(v8::Isolate* isolate, v8::Local<v8::Object>& _sentinel, v8::Local<v8::Function>& _callback)
	{
		sentinel.Reset(isolate, _sentinel);
		callback.Reset(isolate, _callback);
		sentinel.SetWeak<_SentinelData>(this, _on_destory, v8::WeakCallbackType::kParameter);
		sentinel.MarkIndependent();
	}

	~_SentinelData()
	{

		v8::Isolate* isolate = v8::Isolate::GetCurrent();
		v8::HandleScope handle_scope(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, callback);
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
						v8::Local<v8::StackFrame> frame = backtrace->GetFrame((uint32_t)i);
						v8::String::Utf8Value path(frame->GetScriptName());
						v8::String::Utf8Value func(frame->GetFunctionName());
#ifdef LOG_ERROR_ENABLED
						int line = frame->GetLineNumber();
						int col  = frame->GetColumn();
						LOG_ERROR("[%d] at %s(%s:%d:%d)",i, *func, *path, line, col);
#endif
					}
				}
#endif
			}
		}
		sentinel.Reset();
		callback.Reset();
	}
};

_JSFUNCTION(sentinel)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(1);
	_READ_FUNC(func, 0);

	v8::Local<v8::Object> obj = v8::Object::New(isolate);
	new _SentinelData(isolate, obj, func);
	args.GetReturnValue().Set(obj);
}

_JSFUNCTION(handle_dispose)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(1);
	_READ_I32(handle_s, 0);

	uint32_t handle = (uint32_t)handle_s;

	if(ERROR_CODE(uint32_t) == handle) _JS_THROW(Error, "Invalid handle id");

	Servlet::ObjectPool::Pool* pool = Servlet::Context::get_object_pool();
	if(NULL == pool) _JS_THROW(Error, "Internal Error");

	if(ERROR_CODE(int) == pool->dispose_object(handle))
	    _JS_THROW(Error, "Cannot dispose object");

	return;
}

_JSFUNCTION(blob_size)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(1);
	_READ_I32(handle_s, 0);
	uint32_t handle = (uint32_t)handle_s;
	if(ERROR_CODE(uint32_t) == handle) _JS_THROW(Error, "Invalid handle id");

	Servlet::ObjectPool::Pool* pool = Servlet::Context::get_object_pool();
	if(NULL == pool) _JS_THROW(Error, "Internal Error: Cannot get object pool");

	Servlet::ObjectPool::Pool::Pointer<Servlet::Blob> blob = pool->get<Servlet::Blob>(handle);

	if(blob.is_null()) _JS_THROW(Error, "Blob not found");

	args.GetReturnValue().Set((int32_t)blob->size());
}

/**
 * @brief read bytes from blob
 * @param handle the handle object we used
 * @param size the number of bytes to read
 * @return the buffer contains the read result
 **/
static inline char* _read_blob(uint32_t handle, int32_t offset, int32_t& size)
{
	if(offset < 0 || size < 0 || handle == ERROR_CODE(uint32_t))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	Servlet::ObjectPool::Pool* pool = Servlet::Context::get_object_pool();
	if(NULL == pool) ERROR_PTR_RETURN_LOG("Cannot get the object pool");

	Servlet::ObjectPool::Pool::Pointer<Servlet::Blob> blob = pool->get<Servlet::Blob>(handle);
	if(blob.is_null()) ERROR_PTR_RETURN_LOG("Blob not found");

	if(size == 0) size = (int32_t)blob->size() - offset;
	char* buffer = &(*blob)[(size_t)offset];

	/* If the first bytes is actually out of the buffer size, we need to create new space for them */
	if((uint32_t)offset + (uint32_t)size > blob->size())
	{
		LOG_DEBUG("The size beyond the buffer boundary");
		uint32_t append_size = (uint32_t)(offset + size + 1) - (uint32_t)blob->size();

		if(blob->ensure_space(append_size) == ERROR_CODE(int))
		    ERROR_PTR_RETURN_LOG("Cannot resize buffer");

		buffer = &(*blob)[(size_t)offset];

		memset(buffer + (blob->size() - (uint32_t)offset), 0, append_size);

		if(blob->append(append_size) == ERROR_CODE(int))
		    ERROR_PTR_RETURN_LOG("Cannot allocate buffer");
	}

	return &(*blob)[(size_t)offset];
}

_JSFUNCTION(blob_get)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(4);
	_READ_I32(handle_s, 0);
	_READ_I32(offset, 1);
	_READ_I32(size, 2);
	_READ_I32(retstr, 3);  //if retstr == 1, return a string instead of an array buffer

	char* buffer;

	if(NULL == (buffer = _read_blob((uint32_t)handle_s, offset, size)))
	    _JS_THROW(Error, "Blob read error");

	v8::Local<v8::Value> result;

	LOG_DEBUG("%d bytes has been read", size);

	if(retstr)
	    result = v8::String::NewFromUtf8(isolate, buffer, v8::String::NewStringType::kNormalString, size);
	else
	    result = v8::ArrayBuffer::New(isolate, buffer, (size_t)size);

	args.GetReturnValue().Set(result);
}

_JSFUNCTION(write)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(2);
	_READ_I32(pipe_s, 0);

	pipe_t pipe = (pipe_t)pipe_s;
	if(ERROR_CODE(pipe_t) == pipe) _JS_THROW(Error, "Invalid arguments");

	size_t rc = ERROR_CODE(size_t);

	if(args[1]->IsArrayBuffer())
	{
		const char* data = NULL;
		v8::Local<v8::ArrayBuffer> buffer = v8::Local<v8::ArrayBuffer>::Cast(args[1]);
		if(buffer.IsEmpty())
		    _JS_THROW(Error, "Invalid buffer");
		data = (const char*)buffer->GetContents().Data();

		if(NULL != data)
		    rc = pipe_write(pipe, data, buffer->ByteLength());
	}
	else
	{
		_READ_STR(data, 1);
		if(NULL != data)
		    rc = pipe_write(pipe, data, strlen(data));
	}

	if(ERROR_CODE(size_t) == rc) _JS_THROW(Error, "Pipe write error");
	args.GetReturnValue().Set((uint32_t)rc);
}

_JSFUNCTION(eof)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(1);
	_READ_I32(pipe_s, 0);
	pipe_t pipe = (pipe_t)pipe_s;
	if(ERROR_CODE(pipe_t) == pipe) _JS_THROW(Error, "Invalid arguments");

	int rc = pipe_eof(pipe);
	if(rc == ERROR_CODE(int)) _JS_THROW(Error, "Could not complete pipe_eof call");

	bool result = (rc != 0);

	args.GetReturnValue().Set(result);
}

_JSFUNCTION(set_flag)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(3);
	_READ_I32(pipe_s, 0);
	_READ_I32(flag_s, 1);
	_READ_BOOL(value, 2);

	pipe_t pipe = (pipe_t)pipe_s;
	pipe_flags_t flags = (pipe_flags_t)flag_s;

	if(pipe == ERROR_CODE(pipe_t) || flags == ERROR_CODE(pipe_flags_t))
	    _JS_THROW(Error, "Invalid arguments");

	if(value)
	{
		LOG_DEBUG("Set flags from javascript %x", flags);
		if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_SET_FLAG, flags))
		    _JS_THROW(Error, "Cannot set flags");
	}
	else
	{
		LOG_DEBUG("Clear flags from javascript %x", flags);
		if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_CLR_FLAG, flags))
		    _JS_THROW(Error, "Cannot clear flags");
	}

}

_JSFUNCTION(get_flags)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(1);
	_READ_I32(pipe_s, 0);

	pipe_t pipe = (pipe_t)pipe_s;
	if(pipe == ERROR_CODE(pipe_t))
	    _JS_THROW(Error, "Invalid arguments");

	pipe_flags_t result;
	if(ERROR_CODE(int) == pipe_cntl(pipe, PIPE_CNTL_GET_FLAGS, &result))
	    _JS_THROW(Error, "Cannot read the pipe flags");

	args.GetReturnValue().Set(result);
}

_JSFUNCTION(unread)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(3);
	_READ_I32(pipe_s, 0);
	_READ_I32(handle_s, 1);
	_READ_I32(offset_s, 2);

	pipe_t pipe = (pipe_t) pipe_s;
	uint32_t handle = (uint32_t)handle_s;
	size_t offset = (size_t)offset_s;

	if(ERROR_CODE(pipe_t) == pipe || ERROR_CODE(uint32_t) == handle || ERROR_CODE(size_t) == offset)
	    _JS_THROW(Error, "Invalid arguments");

	Servlet::ObjectPool::Pool* obj_pool = Servlet::Context::get_object_pool();
	if(NULL == obj_pool)
	    _JS_THROW(Error, "Internal Error: Cannot get the object pool for current thread");

	Servlet::ObjectPool::Pool::Pointer<Servlet::Blob> ptr = obj_pool->get<Servlet::Blob>(handle);
	if(ptr.is_null())
	    _JS_THROW(Error, "Blob object not found");

	const char* buffer = &(*ptr)[0];

	int rc = pipe_cntl(pipe, PIPE_CNTL_EOM, buffer, offset);
	if(rc == ERROR_CODE(int))
	    _JS_THROW(Error, "Cannot complete pipe operation PIPE_CNTL_EOM");
}

static inline int _dispose_state(void* state)
{
	if(NULL == state) ERROR_RETURN_LOG(int, "Invalid arguments");
	free(state);
	return 0;
}

_JSFUNCTION(push_state)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(2);
	_READ_I32(pipe_s, 0);
	_READ_STR(data, 1);

	pipe_t pipe = (pipe_t)pipe_s;
	if(ERROR_CODE(pipe_t) == pipe)
	    _JS_THROW(Error, "Invalid arguments");

	size_t len = strlen(data);
	//TODO: use the memory pool
	char* state = (char*)malloc(len + 1);
	if(NULL == state)
	    _JS_THROW(Error, "Internal Error: Cannot allocate memory for the new state");

	memcpy(state, data, len + 1);
	int rc = pipe_cntl(pipe, PIPE_CNTL_PUSH_STATE, state, _dispose_state);

	if(ERROR_CODE(int) == rc)
	    _JS_THROW(Error, "Cannot complete pipe operation PIPE_CNTL_PUSH_STATE");
}

_JSFUNCTION(pop_state)
{
	_JSFUNCTION_INIT;
	_CHECK_ARGC(1);
	_READ_I32(pipe_s, 0);

	pipe_t pipe = (pipe_t)pipe_s;
	if(ERROR_CODE(pipe_t) == pipe)
	    _JS_THROW(Error, "Invalid arguments");

	const char* state = NULL;
	int rc = pipe_cntl(pipe, PIPE_CNTL_POP_STATE, &state);
	if(ERROR_CODE(int) == rc)
	    _JS_THROW(Error, "Cannot complete operation PIPE_CNTL_POP_STATE");

	/* If the state is empty, return undefined directly */
	if(NULL == state) return;

	v8::Local<v8::Value> result = v8::String::NewFromUtf8(isolate, state);
	if(result.IsEmpty())
	    _JS_THROW(Error, "Internal Error: Cannot construct state variable");

	args.GetReturnValue().Set(result);
}


namespace Servlet {
	int builtin_init(Servlet::Context* context)
	{
#define _BUILTIN(name)\
		if(ERROR_CODE(int) == context->builtin_func("__"#name, _##name))\
		    ERROR_RETURN_LOG(int, "Cannot register builtin function _%s", #name)
		_BUILTIN(define);
		_BUILTIN(log);
		_BUILTIN(import);
		_BUILTIN(read);
		_BUILTIN(gc);
		_BUILTIN(sentinel);
		_BUILTIN(handle_dispose);
		_BUILTIN(blob_size);
		_BUILTIN(blob_get);
		_BUILTIN(write);
		_BUILTIN(eof);
		_BUILTIN(set_flag);
		_BUILTIN(get_flags);
		_BUILTIN(unread);
		_BUILTIN(push_state);
		_BUILTIN(pop_state);
		return 0;
	}
}

