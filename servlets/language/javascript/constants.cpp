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
#include <destructorqueue.hpp>
#include <context.hpp>

template <unsigned Value>
struct U32 {
	static inline int32_t GetRuntimeValue()
	{
		return (int32_t)Value;
	}
};

struct Version {
	static inline v8::Local<v8::String> GetRuntimeValue()
	{
		v8::Isolate* isolate = v8::Isolate::GetCurrent();
		return v8::String::NewFromUtf8(isolate, runtime_version());
	}
};

template <typename Value>
class Constant {
	static inline void _getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info)
	{
		(void)property;
		info.GetReturnValue().Set(Value::GetRuntimeValue());
	}
	inline int _setup()
	{
		if(ERROR_CODE(int) == _context->constant(_name, _getter))
		    ERROR_RETURN_LOG(int, "Cannot register the builtin constant %s", _name);
		return 0;
	}
	Servlet::Context* _context;
	const char* _name;
	public:

	Constant(Servlet::Context* context, const char* name)
	{
		_context = context;
		_name = name;
	}
	~Constant()
	{
		_setup();
	}
};

namespace Servlet {
	int constants_init(Servlet::Context* context)
	{
		Constant<Version>                     (context, "__PLUMBER_RUNTIME_VERSION");
		Constant<U32<PIPE_INPUT> >            (context, "__PIPE_INPUT");
		Constant<U32<PIPE_OUTPUT> >           (context, "__PIPE_OUTPUT");
		Constant<U32<PIPE_ASYNC> >            (context, "__PIPE_ASYNC");
		Constant<U32<PIPE_SHADOW> >           (context, "__PIPE_SHADOW");
		Constant<U32<PIPE_PERSIST> >          (context, "__PIPE_PERSIST");
		Constant<U32<PIPE_DISABLED> >         (context, "__PIPE_DISABLED");

		Constant<U32<PIPE_CNTL_GET_FLAGS> >   (context, "__PIPE_CNTL_GET_FLAGS");
		Constant<U32<PIPE_CNTL_SET_FLAG> >    (context, "__PIPE_CNTL_SET_FLAG");
		Constant<U32<PIPE_CNTL_CLR_FLAG> >    (context, "__PIPE_CNTL_CLR_FLAG");
		Constant<U32<PIPE_CNTL_EOM> >         (context, "__PIPE_CNTL_EOM");
		Constant<U32<PIPE_CNTL_POP_STATE> >   (context, "__PIPE_CNTL_POP_STATE");
		Constant<U32<PIPE_CNTL_PUSH_STATE> >  (context, "__PIPE_CNTL_PUSH_STATE");
		Constant<U32<PIPE_CNTL_INVOKE> >      (context, "__PIPE_CNTL_INVOKE");
		Constant<U32<PIPE_CNTL_NOP> >         (context, "__PIPE_CNTL_NOP");

		Constant<U32<FATAL> >                 (context, "__FATAL");
		Constant<U32<ERROR> >                 (context, "__ERROR");
		Constant<U32<WARNING> >               (context, "__WARNING");
		Constant<U32<NOTICE> >                (context, "__NOTICE");
		Constant<U32<TRACE> >                 (context, "__TRACE");
		Constant<U32<INFO> >                  (context, "__INFO");
		Constant<U32<DEBUG> >                 (context, "__DEBUG");

		return 0;
	}
}
