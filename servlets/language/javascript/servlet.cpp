/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <exception>
#include <pservlet.h>
#include <pstd.h>

#include <v8engine.hpp>

#include <blob.hpp>
#include <objectpool.hpp>
#include <destructorqueue.hpp>
#include <context.hpp>
#include <constants.hpp>
#include <builtin.hpp>

using namespace std;

int init(uint32_t argc, char const* const* argv, void* data)
{
	if(argc < 2)
	    ERROR_RETURN_LOG(int, "Javascript Servlet expectes at least one argument");

	Servlet::Context* context = NULL;
	try
	{
		context = new(data)Servlet::Context();

		if(ERROR_CODE(int) == Servlet::constants_init(context))
		    ERROR_RETURN_LOG(int, "Cannot initialize the constants");

		if(ERROR_CODE(int) == Servlet::builtin_init(context))
		    ERROR_RETURN_LOG(int, "Cannot initialize the builtin functions");

		//      Because V8 dosen't allow the context forked to another isolate, so we
		//      Need to initialize the isolate each time a new thread is asking for the context.
		//      For the first time thread ask for the context, the init function should be called
		//      (which will happens in this function). To clearify, what needs to be done:
		//      1. Load the script
		//      2. Run the script
		//      3. Call __servlet_def__.init(args) for Context (And JSONify)
		//		After that, the context request must come from the exec function, in this case,
		//		we need to do
		//		1. Run the script
		//		2. Inject servlet context to the context (parse from JSON)

		if(context == NULL || ERROR_CODE(int) == context->setup(argv[1], argc - 2, argv + 2))
		    ERROR_RETURN_LOG(int, "Cannot call the initialization function");
	}
	catch(std::exception& ex)
	{
		LOG_ERROR("C++ Exception: %s", ex.what());
		ERROR_RETURN_LOG(int, "Cannot initialize the context: %s", ex.what());
	}
	if(context->ensure_thread_ready() == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot initialize the thread local context");

	return 0;
}

int exec(void* data)
{
	Servlet::Context* context = (Servlet::Context*)data;
	return context->exec();
}

int unload(void* data)
{
	Servlet::Context* context = (Servlet::Context*)data;
	context->~Context();
	return 0;
}

PSERVLET_EXPORT(Servlet::Context, init, exec, unload, "JavaScript Loader", 0);

