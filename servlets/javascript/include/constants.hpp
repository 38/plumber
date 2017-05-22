/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The constant definition for the V8 environment
 * @file javascript/include/constants.hpp
 **/
#ifndef __JAVASCRIPT_CONSTANTS_HPP__
#define __JAVASCRIPT_CONSTANTS_HPP__
namespace Servlet {
	/**
	 * @brief initialize the constant in the given context
	 * @param context the context to initialize
	 * @return status code
	 **/
	int constants_init(Servlet::Context* context);
}
#endif /* __JAVASCRIPT_CONSTANTS_HPP__ */
