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

#include <blob.hpp>
#include <objectpool.hpp>
#include <isolate.hpp>
#include <destructorqueue.hpp>
#include <context.hpp>
#include <global.hpp>

using namespace std;

/**
 * @brief the helper object which is used to autoamtically dispose memory
 **/
template <typename T>
struct _ScopeWatcher {
	_ScopeWatcher(T* ptr) : _ptr(ptr) {}
	~ _ScopeWatcher() { delete _ptr; }
	private:
	T* _ptr;
};

template <typename T>
struct _ScopeArrayWatcher {
	_ScopeArrayWatcher(T* ptr) : _ptr(ptr) {}
	~ _ScopeArrayWatcher() { delete[] _ptr; }
	private:
	T* _ptr;
};

static v8::Platform* _platform = NULL;
static uint32_t _init_count = 0;
static pstd_thread_local_t* _isolate_collection;
static pstd_thread_local_t* _thread_object_pools;
static pstd_thread_local_t* _thread_descturctor_queues;

static inline void* _isolate_new(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;

	Servlet::Isolate* ret = new Servlet::Isolate();
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot create isolate");

	if(ERROR_CODE(int) == ret->init())
	{
		delete ret;
		ERROR_PTR_RETURN_LOG("Cannot initialize isolate");
	}

	ret->get()->SetCaptureStackTraceForUncaughtExceptions(true);

	return ret;
}

static inline int _isolate_free(void* mem, const void* data)
{
	(void)data;

	if(NULL == mem) ERROR_RETURN_LOG(int, "Invalid arguments");
	delete (Servlet::Isolate*)mem;

	return 0;
}

static inline v8::Isolate* _get_isolate()
{
	Servlet::Isolate* isolate_wrapper = (Servlet::Isolate*)pstd_thread_local_get(_isolate_collection);
	if(NULL == isolate_wrapper)
	    ERROR_PTR_RETURN_LOG("Cannot get the isolate object");
	return isolate_wrapper->get();
}

static inline void* _thread_context_new(uint32_t tid, const void* data)
{
	(void)tid;
	Servlet::Context* context = (Servlet::Context*)data;
	return context->thread_init();
}

static inline int _thread_context_free(void* mem, const void* data)
{
	(void)data;
	if(NULL == data) ERROR_RETURN_LOG(int, "Invalid arguments");

	delete (Servlet::Global*)mem;

	return 0;
}

static inline void* _thread_dqueue_new(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;

	return (void*)(new Servlet::DestructorQueue());
}

static inline int _thread_dqueue_free(void* mem, const void* data)
{
	(void)data;
	
	Servlet::DestructorQueue* queue = (Servlet::DestructorQueue*)mem;

	delete queue;

	return 0;
}

static inline void* _object_pool_new(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;

	Servlet::ObjectPool::Pool* ret = new Servlet::ObjectPool::Pool();

	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create new object pool for thread");

	if(!ret->check_initialized())
	{
		delete ret;
		ERROR_PTR_RETURN_LOG("Cannot initialize the object pool");
	}

	return ret;
}

static inline int _object_pool_free(void* mem, const void* data)
{
	(void)data;
	Servlet::ObjectPool::Pool* pool = (Servlet::ObjectPool::Pool*)mem;

	if(NULL == pool) ERROR_RETURN_LOG(int, "Invalid arguments");

	delete pool;
	return 0;
}

int Servlet::Context::_run_script(v8::Isolate* isolate, v8::Persistent<v8::Context>& context, const char* script, const char* filename)
{
	v8::HandleScope handle_scope(isolate);
	v8::Context::Scope contextScope(v8::Local<v8::Context>::New(isolate, context));

	v8::TryCatch trycatch(isolate);

	if(Servlet::Context::import_script(isolate, script, filename) == ERROR_CODE(int))
	{
		LOG_ERROR("Cannot run script %s", filename);
#if LOG_LEVEL >= ERROR
		v8::Local<v8::Message> message = trycatch.Message();
		v8::String::Utf8Value exception_str(message->Get());
		LOG_ERROR("Uncaught Javascript Exception: %s", *exception_str);
		v8::Local<v8::StackTrace> backtrace = message->GetStackTrace();
		if(!backtrace.IsEmpty())
		{
			int frame_count = backtrace->GetFrameCount();
			for(uint32_t i = 0; i < (uint32_t)frame_count; i ++)
			{
				v8::Local<v8::StackFrame> frame = backtrace->GetFrame(i);
				v8::String::Utf8Value path(frame->GetScriptName());
				v8::String::Utf8Value func(frame->GetFunctionName());
#ifdef LOG_ERROR_ENABLED
				int line = frame->GetLineNumber();
				int col  = frame->GetColumn();
				LOG_ERROR("[%d] at %s(%s:%d:%d)",i, *func, *path, line, col);
#endif
			}
		}
		return ERROR_CODE(int);
#endif
	}

	return 0;
}

v8::Handle<v8::Function> Servlet::Context::_get_servlet_function(v8::Local<v8::Context>& context, const char* name)
{
	(void) name;

	v8::Isolate* isolate = _get_isolate();

	v8::Handle<v8::Object> object = v8::Local<v8::Object>::Cast(context->Global()->Get(v8::String::NewFromUtf8(isolate, "__servlet_def__")));

	if(object.IsEmpty())
	{
		LOG_ERROR("Cannot read variable __servlet_def__");
		return v8::Handle<v8::Function>();
	}

	v8::Handle<v8::Function> function = v8::Local<v8::Function>::Cast(object->Get(v8::String::NewFromUtf8(isolate, name)));

	return function;
}


/**************************************************Public Methods *********************************************/

Servlet::Context::Context()
{
	_thread_context = NULL;
	_main_script = NULL;
	_main_script_filename = NULL;
	_context_json = NULL;
}

Servlet::Context::~Context()
{

	if(!--_init_count)
	{
		if(NULL != _isolate_collection) pstd_thread_local_free(_isolate_collection);
		if(NULL != _thread_object_pools) pstd_thread_local_free(_thread_object_pools);
		if(NULL != _thread_descturctor_queues) pstd_thread_local_free(_thread_descturctor_queues);
		v8::V8::Dispose();
		v8::V8::ShutdownPlatform();
		delete _platform;
	}

	if(NULL != _thread_context)
	    pstd_thread_local_free(_thread_context);

	if(NULL != _main_script) delete[] _main_script;
	if(NULL != _main_script_filename) delete[] _main_script_filename;
	if(NULL != _context_json) delete[] _context_json;
}
void* Servlet::Context::thread_init()
{
	Servlet::Global* ret = NULL;
#define _E(msg) { LOG_ERROR(msg); break; }
	do{
		/* Create the new servlet global */
		ret = new Servlet::Global(this);
		if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create new global");

		if(ERROR_CODE(int) == ret->init())
		    _E("Cannot initialize the thread local global");

		/* Run the main script */
		v8::Isolate* isolate = _get_isolate();
		if(NULL == isolate)
		    _E("Cannot get current isolate");

		v8::Persistent<v8::Context>& context = ret->get();

		if(this->_run_script(isolate, context, "__import(\"__init__.js\");", "<initializer>") == ERROR_CODE(int))
		    _E("Cannot run the initializer code");

		if(this->_run_script(isolate, context, _main_script, _main_script_filename) == ERROR_CODE(int))
		    _E("Cannot run the main script code");

		if(_context_json == NULL)
		{
			v8::HandleScope handle_scope(isolate);
			v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, ret->get());
			v8::Context::Scope scope = v8::Context::Scope(context);
			v8::Local<v8::Function> func = _get_servlet_function(context, "init");
			if(func.IsEmpty()) _E("init is not a function");

			v8::Handle<v8::Value>* args = new v8::Handle<v8::Value>[_argc];
			_ScopeArrayWatcher<v8::Handle<v8::Value> > scope_watcher(args);

			for(uint32_t i = 0; i < _argc; i ++)
			    args[i] = v8::String::NewFromUtf8(isolate, _argv[i]);

			v8::TryCatch trycatch(isolate);
			v8::Handle<v8::Value> result = func->Call(context->Global(), (int)_argc, args);

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
						v8::Local<v8::StackFrame> frame = backtrace->GetFrame((unsigned)i);
						v8::String::Utf8Value path(frame->GetScriptName());
						v8::String::Utf8Value func(frame->GetFunctionName());
#if LOG_LEVEL_ENABLED
						int line = frame->GetLineNumber();
						int col  = frame->GetColumn();
						LOG_ERROR("[%d] at %s(%s:%d:%d)",i, *func, *path, line, col);
#endif
					}
				}
#endif
				_E("Cannot initialize the servlet context");
			}
			else
			{
				v8::String::Utf8Value str(result);

				_context_json = new char[strlen(*str) + 1];
				if(NULL == _context_json) _E("Cannot allocate buffer");
				snprintf(_context_json, strlen(*str) + 1, "%s", *str);
			}
		}
		return ret;
	} while(0);
#undef _E
	if(ret != NULL) delete ret;
	return NULL;
}

int Servlet::Context::exec()
{
	v8::Isolate* isolate = _get_isolate();
	if(NULL == isolate) ERROR_RETURN_LOG(int, "Cannot get the isolate for current thread");

	Servlet::Global* servlet_global = (Servlet::Global*)pstd_thread_local_get(_thread_context);
	if(NULL == servlet_global) ERROR_RETURN_LOG(int, "Cannot acquire the thread local context");

	v8::HandleScope handle_scope(isolate);
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, servlet_global->get());
	v8::Context::Scope scope = v8::Context::Scope(context);

	v8::Local<v8::Function> func = _get_servlet_function(context, "exec");
	if(func.IsEmpty()) ERROR_RETURN_LOG(int, "Cannot get exec function");

	v8::Handle<v8::Value> argv[1];
	argv[0] = v8::String::NewFromUtf8(isolate, _context_json);

	v8::TryCatch trycatch(isolate);
	v8::Handle<v8::Value> result = func->Call(context->Global(), 1, argv);
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
				v8::Local<v8::StackFrame> frame = backtrace->GetFrame((unsigned)i);
				v8::String::Utf8Value path(frame->GetScriptName());
				v8::String::Utf8Value func(frame->GetFunctionName());
#if LOG_LEVEL_ENABLED
				int line = frame->GetLineNumber();
				int col  = frame->GetColumn();
				LOG_ERROR("[%d] at %s(%s:%d:%d)",i, *func, *path, line, col);
#endif
			}
		}
#endif
		return ERROR_CODE(int);
	}

	return 0;
}

int Servlet::Context::builtin_func(const char* name, v8::FunctionCallback func)
{
	if(NULL == name || NULL == func)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_func_list.push_back(make_pair(name, func));

	return 0;
}

int Servlet::Context::constant(const char* name, v8::AccessorGetterCallback callback)
{
	if(NULL == name || NULL == callback)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_const_list.push_back(make_pair(name, callback));

	return 0;
}

int Servlet::Context::setup(const char* filename, uint32_t argc, char const * const * argv)
{
	if(_init_count == 0)
	{
		if(NULL == (_isolate_collection = pstd_thread_local_new(_isolate_new, _isolate_free, NULL)))
		    ERROR_RETURN_LOG(int, "Cannot create thread local for the threads");

		if(NULL == (_thread_object_pools = pstd_thread_local_new(_object_pool_new, _object_pool_free, NULL)))
		{
			pstd_thread_local_free(_isolate_collection);
			ERROR_RETURN_LOG(int, "Cannot create thread local object pool");
		}
	
		if(NULL == (_thread_descturctor_queues = pstd_thread_local_new(_thread_dqueue_new, _thread_dqueue_free, this)))
		{
			pstd_thread_local_free(_isolate_collection);
			pstd_thread_local_free(_thread_object_pools);
			ERROR_RETURN_LOG(int, "Cannot create thread local for the destructor queue");
		}


		v8::V8::InitializeICUDefaultLocation(PLUMBER_V8_BLOB_DATA_PATH);
		v8::V8::InitializeExternalStartupData(PLUMBER_V8_BLOB_DATA_PATH);

		if(NULL == (_platform = v8::platform::CreateDefaultPlatform()))
		    ERROR_RETURN_LOG(int, "Cannot create platform");

		v8::V8::InitializePlatform(_platform);
		v8::V8::Initialize();
	}
	_init_count ++;

	if(NULL == (_thread_context = pstd_thread_local_new(_thread_context_new, _thread_context_free, this)))
	    ERROR_RETURN_LOG(int, "Cannot create thread local for the global context");

	_main_script = Servlet::Context::load_script_from_file(filename);

	if(NULL == _main_script) ERROR_RETURN_LOG(int, "Cannot load script file");

	_main_script_filename = new char[strlen(filename) + 1];
	snprintf(_main_script_filename, strlen(filename) + 1, "%s", filename);

	_argc = argc;
	_argv = argv;

	return 0;
}

int Servlet::Context::ensure_thread_ready()
{       
	if(NULL == pstd_thread_local_get(_thread_context))
		ERROR_RETURN_LOG(int, "The thread local context is not initialized");
	return 0;
}

/******************************** Static Methods **************************************************/

char* Servlet::Context::load_script_from_file(const char* filename, const char* header, const char* trailer)
{
	if(NULL == filename)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const char* script_path = NULL;
	char path_buffer[PATH_MAX];
	struct stat stat_res;
	const char* paths[] = {secure_getenv("JSPATH"), INSTALL_PREFIX"/lib/plumber/javascript", NULL};
	if(access(filename, R_OK) != F_OK || lstat(filename, &stat_res) != 0 || !S_ISREG(stat_res.st_mode))
	{
		for(int i = paths[0] == NULL ? 1 : 0; paths[i] != NULL; i ++)
		{
			const char* js_path = paths[i];
			size_t len = 0;
			for(const char* ptr = js_path; ; ptr ++)
			{
				if(*ptr == ':' || *ptr == 0)
				{
					snprintf(path_buffer + len, PATH_MAX - len, "/%s", filename);
					if(access(path_buffer, R_OK) == F_OK)
					{
						script_path = path_buffer;
						break;
					}
					len = 0;
				}
				else if(len < PATH_MAX - 1) path_buffer[len++] = *ptr;
				if(*ptr == 0) break;
			}
		}
	}
	else script_path = filename;

	if(NULL == script_path)
	    ERROR_PTR_RETURN_LOG("Cannot find script file %s", filename);
	else
	    LOG_INFO("Source code file %s has been loaded", filename);

	FILE* fp = fopen(script_path, "r");
	long script_size;
	long header_size = header == NULL ? 0 : (long)strlen(header);
	long trailer_size = trailer == NULL ? 0 : (long)strlen(trailer);
	char* script = NULL;
	size_t size;

	if(NULL == fp) ERROR_PTR_RETURN_LOG_ERRNO("Cannot open script file");

	if(0 != fseek(fp, 0, SEEK_END)) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot seek file");

	script_size = ftell(fp);
	if(script_size < 0) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot get the size of the script file");

	script = new char[script_size + header_size + trailer_size + 1];

	if(0 != fseek(fp, 0 ,SEEK_SET)) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot reset the buffer pointer");


	size = fread(script + header_size, 1, (size_t)script_size, fp);
	fclose(fp);

	if(size < (size_t)script_size)
	    LOG_WARNING_ERRNO("Cannot read the file %s to the end", script_path);

	memcpy(script, header, (size_t)header_size);
	memcpy(script + header_size + size, trailer, (size_t)trailer_size);

	script[(size_t)header_size + (size_t)trailer_size + (size_t)size] = 0;

	return script;

ERR:
	if(NULL != fp) fclose(fp);
	if(NULL != script) free(script);
	return NULL;
}

v8::Isolate* Servlet::Context::get_isolate()
{
	return _get_isolate();
}

int Servlet::Context::import_script(v8::Isolate* isolate, const char* program_text, const char* filename)
{
	if(NULL == program_text)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(filename == NULL) filename = "<anonymous>";

	if(NULL == isolate) ERROR_RETURN_LOG(int, "Cannot get isolate");

	v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, program_text, v8::NewStringType::kNormal).ToLocalChecked();
	v8::Local<v8::String> origin = v8::String::NewFromUtf8(isolate, filename, v8::NewStringType::kNormal).ToLocalChecked();
	v8::Local<v8::Script> script = v8::Script::Compile(source, origin);

	v8::Local<v8::Value> value = script->Run();

	if(value.IsEmpty()) return ERROR_CODE(int);

	return 0;
}

Servlet::ObjectPool::Pool* Servlet::Context::get_object_pool()
{
	if(NULL == _thread_object_pools) ERROR_PTR_RETURN_LOG("The thread local for object pools hasn't been initialized");
	return (Servlet::ObjectPool::Pool*) pstd_thread_local_get(_thread_object_pools);
}

Servlet::DestructorQueue* Servlet::Context::get_destructor_queue()
{
	if(NULL == _thread_descturctor_queues) ERROR_PTR_RETURN_LOG("The thread local for destructor queue hasn't been initialized");
	return (Servlet::DestructorQueue*) pstd_thread_local_get(_thread_descturctor_queues);
}
