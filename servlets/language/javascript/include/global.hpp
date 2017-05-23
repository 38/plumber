/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The V8 Engine Global Context for a single thread
 * @file javascript/include/global.hpp
 **/
#ifndef __JAVASCRIPT_GLOBAL_HPP__
#define __JAVASCRIPT_GLOBAL_HPP__

namespace Servlet {
	/**
	 * @brief the V8 global context
	 **/
	class Global {
		Servlet::Context*           _servlet_context; /*!< the servlet context */
		v8::Persistent<v8::Context> _v8_context;  /*!< the actual context */
		public:
		/**
		 * @brief create a new global instance from the servlet context
		 **/
		Global(Servlet::Context* context);

		/**
		 * @brief Initialize the global context
		 * @return status code
		 **/
		int init();

		/**
		 * @brief Get the global context for V8 Engine
		 * @return the reference to the v8 engine
		 **/
		v8::Persistent<v8::Context>& get();
	};
}

#endif /*__JAVASCRIPT_GLOBAL_HPP__*/
