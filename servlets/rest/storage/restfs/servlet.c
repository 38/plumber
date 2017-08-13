/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>
typedef struct {
	uint32_t json_mode:1;        /*!< The JSON mode */
	uint32_t modify_time:1;      /*!< If we need change time */
	uint32_t create_time:1;      /*!< If we need the creating time */
	pipe_t  command;             /*!< The storage command input pipe */
	pipe_t  parent_not_exist;    /*!< The input side of the signal for the parent resource is not exist */
	pipe_t  not_exist;           /*!< The signal pipe which will trigger when the exist command is required and the resource is not avaliable */
	pipe_t  data;                /*!< The actual resoure data or the list of resource id in JSON / the raw file RLS token  */
} context_t;
/* TODO: it seems we could have two mode, JSON mode and raw data mode,
 *       For the json mode, we need to provide a schema file so that the servlet
 *       can validate the input is valid. also we can make it automatically add creation time
 *       and/or modification time 
 *       For the raw data we don't need these options 
 **/
SERVLET_DEF = {
	.desc    = "The filesystem based restful storage controller",
	.version = 0,
	.size    = sizeof(context_t)
};
