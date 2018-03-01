/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>

#include <pservlet.h>
#include <pstd.h>

#include <pstd/mempool.h>
#include <pstd/types/trans.h>

typedef struct {
	pstd_scope_stream_t* data_source;  /*!< The data source stream */
	uint32_t   data_source_eos:1;      /*!< Indicates we see end-of-stream marker from the data source */
	
	z_stream*  zlib_stream;         /*!< The zlib stream we are dealing with */
	char*      input_buf;           /*!< The input buffer */
	uint32_t   needs_more_input:1;  /*!< Indicates we needs more input */
} _processor_t;




