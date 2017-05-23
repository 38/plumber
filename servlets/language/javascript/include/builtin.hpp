/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the builtin function infrastructure for Plumber
 * @file javascript/include/builtin.hpp
 **/
#ifndef __JAVASCRIPT_BUILTIN_HPP__
#define __JAVASCRIPT_BUILTIN_HPP__
namespace Servlet {
	/**
	 * @brief initialize the builtin funciton
	 * @return status code
	 **/
	int builtin_init(Servlet::Context* context);
}

#endif /* __JAVASCRIPT_BUILTIN_HPP__ */
