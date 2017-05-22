/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the warpper for a V8 isolate
 * @file javascript/include/isolate.hpp
 **/
#ifndef __JAVASCRIPT_ISOLATE_H__
namespace Servlet {

	/**
	 * @brief the isolate object
	 **/
	class Isolate {
		v8::Isolate::CreateParams    _create_params;    /*!< The create params for the V8 engine */
		v8::Isolate*                 _isolate;          /*!< The actual isolate */
		v8::Isolate::Scope*          _scope;            /*!< The isolate scope */
		public:

		~Isolate();

		/**
		 * @brief initialize the isolate
		 * @return status code
		 **/
		int init();

		/**
		 * @brief Get the V8 Isolate object from the wrapper object
		 * @return The V8 Isolate object
		 **/
		v8::Isolate* get();
	};
}
#endif
