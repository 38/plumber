/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The MacOS related constants
 * @file os/darwin/const.h
 **/
#if !defined(__OS_DARWIN_H__) && defined(__DARWIN__)
#include <limits.h>

/** @brief the prefix for the servlet filename */
#define RUNTIME_SERVLET_FILENAME_PREFIX "lib"

/** @brief the suffix for the servlet filename */
#define RUNTIME_SERVLET_FILENAME_SUFFIX ".dylib"

#define rl_set_signals(...)

#endif
