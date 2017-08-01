/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

/**
 * @brief The context for a rest storage command
 **/
typedef struct _rc_t {
	char*                  res_name;        /*!< The name of the resource */
	char*                  parent_name;     /*!< The name of the parent resource */
	const struct _rc_t*    parent;          /*!< The parent object */ 
	pipe_t                 output;          /*!< The input pipe */
	pstd_type_accessor_t   opcode_acc;      /*!< The opcode accessor */
	pstd_type_accessor_t   object_id_acc;   /*!< The object ID accessor */
	pstd_type_accessor_t   parent_id_acc;   /*!< The parent ID accessor */
	pstd_type_accessor_t   content_acc;     /*!< The content accessor */
	pstd_type_accessor_t   param_acc;       /*!< The parameter accessor */
} resource_ctx_t;

/**he         
 * @param The servlet context
 **/
typedef struct {
	pipe_t                 request;         /*!< The request used to analyze */
	resource_ctx_t*        resources;       /*!< The storage command to perfome */
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

static inline int _fill_const(pstd_type_model_t* model, pipe_t pipe, const char* field, uint32_t* buf)
{
	if(ERROR_CODE(int) == PSTD_TYPE_MODEL_ADD_CONST(model, pipe, field, buf))
		ERROR_RETURN_LOG(int, "Cannot read the constant named %s from pipe 0x%x", field, pipe);
	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	if(argc < 2)
		ERROR_RETURN_LOG(int, "Usage: %s [parent:resource] | [resource]", argv[0]);
	context_t* ctx = (context_t*)ctxbuf;
	ctx->model    = NULL;

	if(NULL == (ctx->resources = (resource_ctx_t*)calloc(argc - 1, sizeof(ctx->resources[0]))))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the resources");

	if(NULL == (ctx->model = pstd_type_model_new()))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot create the type model");

	if(ERROR_CODE(pipe_t) == (ctx->request = pipe_define("request", PIPE_INPUT, "plumber.std_servlet.network.http.httpreq.Request")))
		ERROR_RETURN_LOG(int, "Cannot define the requested pipe");

#define _READ_CONST_CHK(p, base, name) (ERROR_CODE(int) ==  _fill_const(ctx->model, ctx->p, #name, &ctx->base.name))
	if(_READ_CONST_CHK(request, method_code, GET) ||
	   _READ_CONST_CHK(request, method_code, POST) ||
	   _READ_CONST_CHK(request, method_code, DELETE))
		return ERROR_CODE(int);

	uint32_t i;

	for(i = 1; i < argc; i ++)
	{
		uint32_t j;
		for(j = 0; argv[i][j] && argv[i][j] != ':'; j ++);
		if(argv[i][j] == ':' && NULL == (ctx->resources[i - 1].parent_name = strndup(argv[i], j)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the parent name");
		const char* resname = argv[i] + ((argv[i][j] == 0) ? 0 : j + 1);
		if(NULL == (ctx->resources[i - 1].res_name = strdup(resname)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the resource name");

		if(ERROR_CODE(pipe_t) == (ctx->resources[i - 1].output = pipe_define(ctx->resources[i - 1].res_name, PIPE_OUTPUT, "plumber.std_servlet.controller.rest.Command")))
			ERROR_RETURN_LOG(int, "Cannot define the request pipe");

		if(i == 1 && (_READ_CONST_CHK(resources[i - 1].output, opcode, CREATE) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, DELETE) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, MODIFY) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, QUERY)  ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, CONTENT)))
			return ERROR_CODE(int);
	}

	return 0;

}

SERVLET_DEF = {
	.desc    = "Simple RESTful API Controller",
	.version = 0x0,
	.size    = sizeof(context_t),
	.init    = _init
};
