/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <options.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t    options;   /*!< The servlet options */
} ctx_t;

SERVLET_DEF = {
	.desc    = "Reads a file from disk",
	.version = 0x0,
	.size    = sizeof(ctx_t);
};

