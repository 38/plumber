/**
 * Copyright (C) 2017, Hao Hou
 **/

#ifndef __SERVLETS_JAVASCRIPT_CONTEXT_H__
#define __SERVLETS_JAVASCRIPT_CONTEXT_H__
namespace Servlet{
	/**
	 * @brief the servlet context
	 **/
	class Context {
		typedef std::vector<std::pair<const char*, v8::FunctionCallback> > BuiltinList;
		typedef std::vector<std::pair<const char*, v8::AccessorGetterCallback> > ConstList;

		pstd_thread_local_t*          _thread_context;   /*!< the context for each thread */
		BuiltinList                   _func_list;
		ConstList                     _const_list;
		char*                         _main_script;
		char*                         _main_script_filename;
		char*                         _context_json;
		uint32_t                      _argc;
		char const* const*            _argv;
		template <class Functor, class List>
		int _for_each(Functor& func, List& list)
		{
			for(typename List::iterator it = list.begin(); it != list.end(); it ++)
			    if(ERROR_CODE(int) == func(it->first, it->second))
			        ERROR_RETURN_LOG(int, "Cannot process list element");
			return 0;
		}

		/**
		 * @brief get a servlet function from current context
		 * @param context the context
		 * @param name the name of the servlet function
		 * @return the function object
		 **/
		v8::Handle<v8::Function> _get_servlet_function(v8::Local<v8::Context>& context, const char* name);

		/**
		 * @brief run the given script
		 * @param context the context used to run the script
		 * @param script the script source code
		 * @param filename the filename
		 * @return status code
		 **/
		int _run_script(v8::Isolate* isolate, v8::Persistent<v8::Context>& context, const char* script, const char* filename);

		public:

		template <class Functor>
		int for_each_function(Functor func)
		{
			return this->_for_each<Functor, BuiltinList>(func, _func_list);
		}

		template <class Functor>
		int for_each_const(Functor func)
		{
			return this->_for_each<Functor, ConstList>(func, _const_list);
		}

		/**
		 * @brief the constructor
		 * @return nothing
		 **/
		Context();

		/**
		 * @brief dispose the class
		 * @return nothing
		 **/
		~Context();

		/**
		 * @brief register a new built-in function in the global object
		 * @note this function should be called before init function be called
		 * @param name the name of the function
		 * @param func the built-in function implementation
		 * @return status code
		 **/
		int builtin_func(const char* name, v8::FunctionCallback func);

		/**
		 * @brief register a new constant to the global object
		 * @note this function should be called before init function called
		 * @paran name the name of the function
		 * @param callback the callback of the accesor
		 * @return status code
		 **/
		int constant(const char* name, v8::AccessorGetterCallback callback);

		/**
		 * @brief load the main script fron the disk
		 * @param filename the file name to run
		 * @return status code
		 **/
		int setup(const char* filename, uint32_t argc, char const * const * argv);

		/**
		 * @brief initialize the thread global context
		 * @param argc the number of arguments
		 * @param argv the argument values
		 * @return Global context
		 **/
		void* thread_init();

		/**
		 * @brief invoke the exec function of the servlet
		 * @return the status code
		 **/
		int exec();

		/**
		 * @brief ensure the thread is in ready-to-run state
		 * @return status code
		 **/
		int ensure_thread_ready();

		/**
		 * @brief get the isolate for current thread
		 * @return the isolate object
		 **/
		static v8::Isolate* get_isolate();

		/**
		 * @brief load the scrpt from file
		 * @param filename the target file name
		 * @param header the header need to append to the file content, this is useful, when you do Nodejs styles of import
		 * @param trailer the trailer string append to the file content
		 * @note you may need to dispose the result buffer after it's used
		 * @return the file context or NULL
		 **/
		static char* load_script_from_file(const char* filename, const char* header = NULL, const char* trailer = NULL);

		/**
		 * @brief import a piece of code to the global
		 * @param isolate the V8 Isolate
		 * @param program_text the program text to import
		 * @param filename the filename for this program text
		 * @return status code
		 **/
		static int import_script(v8::Isolate* isolate, const char* program_text, const char* filename);

		/**
		 * @biref Get the object pool for current thread
		 * @return the object pool for current thread
		 **/
		static Servlet::ObjectPool::Pool* get_object_pool();

		/**
		 * @brief Get the destructor queue for current thread
		 * @return The destructor queue for this thread
		 **/
		static Servlet::DestructorQueue* get_destructor_queue();
	};
}
#endif /* __SERVLETS_JAVASCRIPT_CONTEXT_H__ */
