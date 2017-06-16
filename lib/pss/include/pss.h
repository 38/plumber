/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The Plumber Service Script Interpreter Library
 * @file  lib/pss/include/pss.h
 **/
#ifndef __PSS_PSS_H__
#define __PSS_PSS_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <error.h>

#include <pss/log.h>
#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>
#include <pss/closure.h>
#include <pss/dict.h>
#include <pss/string.h>
#include <pss/vm.h>

#include <pss/lex.h>


/**
 * @brief Initialize the PSS interpreter libray
 * @return status code
 **/
int pss_init();

/**
 * @brief Finalize the PSS interpreter libray
 * @return status code
 **/
int pss_finalize();

#endif /* __PSS_PSS_H__ */
