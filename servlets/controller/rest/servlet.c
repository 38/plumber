/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <uuid/uuid.h>

#include <utils/static_assertion.h>

#include <pservlet.h>
#include <pstd.h>

#include <bsr64.h>

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
STATIC_ASSERTION_FIRST(resource_ctx_t, res_name);

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
	ctx->count = argc - 1;

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

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->method_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "method")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for request.method");
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->path_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "path")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for request.path");
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->param_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "param")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for request.method");
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->data_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "data")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for request.method");

	uint32_t i, j;

	for(i = 1; i < argc; i ++)
	{
		resource_ctx_t* res = ctx->resources + i - 1;
		uint32_t j;
		for(j = 0; argv[i][j] && argv[i][j] != ':'; j ++);
		if(argv[i][j] == ':' && NULL == (res->parent_name = strndup(argv[i], j)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the parent name");
		const char* resname = argv[i] + ((argv[i][j] == 0) ? 0 : j + 1);
		if(NULL == (res->res_name = strdup(resname)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot duplicate the resource name");

		if(ERROR_CODE(pipe_t) == (res->output = pipe_define(res->res_name, PIPE_OUTPUT, "plumber.std_servlet.controller.rest.Command")))
			ERROR_RETURN_LOG(int, "Cannot define the request pipe");

		if(i == 1 && (_READ_CONST_CHK(resources[i - 1].output, opcode, CREATE) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, DELETE) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, MODIFY) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, QUERY)  ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, CONTENT)))
			return ERROR_CODE(int);

		if(ERROR_CODE(pstd_type_accessor_t) == (res->opcode_acc =pstd_type_model_get_accessor(ctx->model, res->output, "opcode")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.opcode", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->parent_id_acc =pstd_type_model_get_accessor(ctx->model, res->output, "parent_id")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.parent_id", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->object_id_acc =pstd_type_model_get_accessor(ctx->model, res->output, "object_id")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.object_id", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->param_acc =pstd_type_model_get_accessor(ctx->model, res->output, "param")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.param", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->content_acc =pstd_type_model_get_accessor(ctx->model, res->output, "content")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.param", ctx->resources[i - 1].res_name);
	}

	qsort(ctx->resources, ctx->count, sizeof(resource_ctx_t), (int (*)(const void*, const void*))strcmp);

	for(i = 0; i < ctx->count; i ++)
	{
		if(ctx->resources[i].parent_name == NULL) continue;
		for(j = 0; j < ctx->count; j ++)
			if(strcmp(ctx->resources[i].parent_name, ctx->resources[j].res_name) == 0)
				ctx->resources[i].parent = ctx->resources + j;
	}

	for(i = 0; i < ctx->count; i ++)
		if(ctx->resources[i].parent_name != NULL && ctx->resources[i].parent == NULL)
			ERROR_RETURN_LOG(int, "Undefined resource type: %s", ctx->resources[i].parent_name);

	return 0;
}

static int _unload(void* ctxbuf)
{
	int rc = 0;
	context_t* ctx = (context_t*)ctxbuf;

	if(NULL != ctx->model && ERROR_CODE(int) == pstd_type_model_free(ctx->model))
		rc = ERROR_CODE(int);

	if(NULL != ctx->resources)
	{
		uint32_t i;
		for(i = 0; i < ctx->count; i ++)
		{
			free(ctx->resources[i].res_name);
			free(ctx->resources[i].parent_name);
		}
		free(ctx->resources);
	}

	return rc;
}
static inline const char* _read_string(pstd_type_instance_t* inst, pstd_type_accessor_t acc)
{
	scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, acc);
	if(ERROR_CODE(scope_token_t) == token) return NULL;

	return (const char*)pstd_scope_get(token);
}

static int _exec(void* ctxbuf)
{
	int rc = 0;
	const context_t* ctx = (context_t*)ctxbuf;

	size_t ti_size = pstd_type_instance_size(ctx->model);
	if(ERROR_CODE(size_t) == ti_size) ERROR_RETURN_LOG(int, "Cannot get the size of the type instance");
	char buf[ti_size];
	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->model, buf);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create the type instance for the type model");

	const char* path = _read_string(inst, ctx->path_acc);

	/* If the path is empty, it means we can not do anything on this request */
	if(NULL == path) return 0;
	while(*path == '/') path ++;
	void* parent_id = NULL;
	uuid_t parent_id_uuid;
	if(*path == '$')
	{
		const char* end = path;
		while(*end && *end != '/') end ++;
		if(sizeof(parent_id_uuid) == bsr64_to_bin(path, end, parent_id_uuid, sizeof(parent_id_uuid)))
			return 0;
		parent_id = parent_id_uuid;
	}

	(void)parent_id;

	rc = 0;
	goto EXIT;
EXIT:
	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");
	return rc;
}

SERVLET_DEF = {
	.desc    = "Simple RESTful API Controller",
	.version = 0x0,
	.size    = sizeof(context_t),
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};
