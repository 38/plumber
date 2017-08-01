/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <stdio.h>
#include <pservlet.h>
#include <pstd.h>

/**
 * @brief The context for a rest storage command
 **/
typedef struct {
	pipe_t                 output;          /*!< The input pipe */
	pstd_type_accessor_t   opcode_acc;      /*!< The opcode accessor */
	pstd_type_accessor_t   object_id_acc;   /*!< The object ID accessor */
	pstd_type_accessor_t   parent_id_acc;   /*!< The parent ID accessor */
	pstd_type_accessor_t   content_acc;     /*!< The content accessor */
	pstd_type_accessor_t   param_acc;       /*!< The parameter accessor */
} command_ctx_t;

/**he         
 * @param The servlet context
 **/
typedef struct {
	pipe_t                 request;         /*!< The request used to analyze */
	command_ctx_t*         commands;        /*!< The storage command to perfome */
	uint32_t count;                         /*!< The number of outputs */
	struct {
		uint32_t           CREATE;          /*!< Create a resource */
		uint32_t           DELETE;          /*!< Delete a resource */
		uint32_t           QUERY;           /*!< Query a resource */
		uint32_t           MODIFY;          /*!< Modify a resource */
		uint32_t           CONTENT;         /*!< Get the content of a source */
	}                      opcode;          /*!< The operation code defined by the protocol */
	struct {
		uint32_t           GET;             /*!< GET HTTP request */
		uint32_t           POST;            /*!< POST HTTP request */
		uint32_t           DELETE;          /*!< DELETE HTTP request */
	}                      method_code;     /*!< The method code */
	pstd_type_model_t*     model;           /*!< The type model */
	pstd_type_accessor_t   method_acc;      /*!< The method accessor */
	pstd_type_accessor_t   path_acc;        /*!< The path accessor */
	pstd_type_accessor_t   param_acc;       /*!< The parameter accessor */
	pstd_type_accessor_t   data_acc;        /*!< The data accessor */
} context_t; 

#if 0
static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;
	ctx->commands = NULL;
	ctx->model    = NULL;

}
#endif

SERVLET_DEF = {
	.desc    = "RESTful API Controller",
	.version = 0x0,
	.size    = sizeof(context_t)
};
