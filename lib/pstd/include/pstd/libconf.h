/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The library configuration utils
 * @file pstd/libconf.h
 **/
#ifndef __PSTD_LIBCONF_H__
#define __PSTD_LIBCONF_H__

/**
 * @brief Try to read the library configuration dynamically, this can be changed by
 *        PSS code:
 *        plumber.std.libconf.&lt;key&gt; = xxxx
 *        If the system can not tell the value, use the default value provided in the param
 * @param key The key to read
 * @param default_val The defualt value
 * @return the value
 **/
int64_t pstd_libconf_read_numeric(const char* key, int64_t default_val);

/**
 * @brief Try to read the string configuration, this function is similar to the numeric version
 * @param key The key to read
 * @param default_val The defualt value
 * @return The value has been loaded
 **/
const char* pstd_libconf_read_string(const char* key, const char* default_val);

#endif /** __PSTD_LIBCONF_H__ */
