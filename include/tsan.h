/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The header file that is used when the Thread Sanitizer is used
 * @include include/tsan.h
 **/
#ifndef __TSAN_H__
#define __TSAN_H__

#if defined(SANITIZER)

/**
 * @brief This is the function prefix that we want to use to prevent Thread Sanitizer instrument 
 *        the function
 **/
#define TSAN_EXCLUDE __attribute__((no_sanitize_thread, noinline))

#else

#define TSAN_EXCLUDE inline

#endif

#endif /* __TSAN_H__ */
