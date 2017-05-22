/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file pservlet.h
 * @brief the top level file for the plumber servlet library
 **/
#ifndef __PSERVLET_H__
#define __PSERVLET_H__

#ifndef __PSERVLET__
#   define __PSERVLET__
#endif

#	ifdef __cplusplus

#	ifdef __clang__
#		pragma clang diagnostic ignored "-Wextern-c-compat"
#	endif /* __clang__ */
extern "C" {
#	endif /* __cplusplus__ */

#include <constants.h>
#include <types.h>
#include <log.h>
#include <pipe.h>
#include <trap.h>
#include <task.h>
#include <runtime.h>
#include <module.h>

	/** @brief the address table that used by table */
	extern const address_table_t* RUNTIME_ADDRESS_TABLE_SYM;

	/**@brief the metadata defined in the plugin */
	extern servlet_def_t RUNTIME_SERVLET_DEFINE_SYM;

#define SERVLET_DEF servlet_def_t RUNTIME_SERVLET_DEFINE_SYM

#	ifdef __cplusplus
};


#		if __cplusplus >= 201103L
#			define _M_T(type, member) decltype(((type*)0)->member)
#		else
#			define _M_T(type, member) typeof(((type*)0)->member)
#		endif

extern "C++" {
	/**
	 * @brief Because C++ doesn't support C99 externsion, so we have to use this awful way do initialization
	 * @param Context the context type
	 * @param desc the description string
	 * @param version the version code
	 * @param init the init callback
	 * @param exec the exec callback
	 * @param unload the unload callback
	 * @return the reference to the the constructed servlet definition
	 **/
	template <class Context>
	static inline servlet_def_t& pservlet_define(_M_T(servlet_def_t, init) init,
	                                             _M_T(servlet_def_t, exec) exec,
	                                             _M_T(servlet_def_t, unload) unload,
	                                             const char* desc = "", uint32_t version = 0)
	{
		static servlet_def_t ret = {};
		ret.init = init;
		ret.exec = exec;
		ret.unload = unload;
		ret.desc = desc;
		ret.version = version;
		ret.size = sizeof(Context);

		return ret;
	}
}
#		undef _M_T

#		define PSERVLET_EXPORT(context, args...) \
    extern "C" {\
	    SERVLET_DEF = pservlet_define<context>(args);\
    }
#	endif /* __cplusplus__ */

/**
 * @brief the mark the symbol should be used within the servlet
 **/
#define __PSERVLET_PRIVATE__ __attribute__((__visibility__("hidden")))

/**
 * @brief mark the symbol can be used externally
 **/
#define __PSERVLET_PUBLIC__ __attribute__((__visibility__("default")))

#endif
