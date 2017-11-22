/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The file used for the intentional fallthrough annotation
 * @file include/fallthrough.h
 **/
#ifndef __PLUMBER_FALLTHROUGH_H__
#define __PLUMBER_FALLTHROUGH_H__
#if defined(__GNUC__) && __GNUC__ >= 7
#	define FALLTHROUGH(...) __attribute__((fallthrough))
#else
#	define FALLTHROUGH(...)
#endif
#endif /* __PLUMBER_FALLTHROUGH_H__ */
