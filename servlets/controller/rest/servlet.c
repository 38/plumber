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
#include <utils/bsr64.h>

#include <pservlet.h>
#include <pstd.h>
#include <pstd/types/string.h>


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

/**
 * @brief The internal object representation
 **/
typedef union {
	uuid_t   uuid;     /*!< The uuid representation */
	uint64_t u64[2];   /*!< The uint64 representation */
	uint8_t  u8[16];   /*!< The uint8 represetnation */
} object_id_t;
STATIC_ASSERTION_SIZE(object_id_t, uuid, sizeof(object_id_t));
STATIC_ASSERTION_SIZE(object_id_t, u64,  sizeof(object_id_t));
STATIC_ASSERTION_SIZE(object_id_t, u8,   sizeof(object_id_t));

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
		uint32_t           EXISTS;          /*!< Check if the resource id exists */
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

static inline int _cmp(const void* a, const void* b)
{
	const resource_ctx_t* ra = (const resource_ctx_t*)a;
	const resource_ctx_t* rb = (const resource_ctx_t*)b;

	return strcmp(ra->res_name, rb->res_name);
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	if(argc < 2)
		ERROR_RETURN_LOG(int, "Usage: %s [parent:resource] | [resource]", argv[0]);
	context_t* ctx = (context_t*)ctxbuf;
	memset(ctx, 0, sizeof(context_t));
	ctx->model    = NULL;
	ctx->count = argc - 1;

	if(NULL == (ctx->resources = (resource_ctx_t*)calloc(argc - 1, sizeof(ctx->resources[0]))))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the resources");

	if(NULL == (ctx->model = pstd_type_model_new()))
		ERROR_RETURN_LOG_ERRNO(int, "Cannot create the type model");

	if(ERROR_CODE(pipe_t) == (ctx->request = pipe_define("request", PIPE_INPUT, "plumber/std_servlet/network/http/httpreq/Request")))
		ERROR_RETURN_LOG(int, "Cannot define the requested pipe");

#define _READ_CONST_CHK(p, base, name) (ERROR_CODE(int) ==  _fill_const(ctx->model, ctx->p, #name, &ctx->base.name))
	if(_READ_CONST_CHK(request, method_code, GET) ||
	   _READ_CONST_CHK(request, method_code, POST) ||
	   _READ_CONST_CHK(request, method_code, DELETE))
		return ERROR_CODE(int);

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->method_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "method")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for request.method");
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->path_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "path.token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for request.path");
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->param_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "param.token")))
		ERROR_RETURN_LOG(int, "Cannot get the accessor for request.method");
	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->data_acc = pstd_type_model_get_accessor(ctx->model, ctx->request, "data.token")))
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

		if(ERROR_CODE(pipe_t) == (res->output = pipe_define(res->res_name, PIPE_OUTPUT, "plumber/std_servlet/controller/rest/Command")))
			ERROR_RETURN_LOG(int, "Cannot define the request pipe");

		if(i == 1 && (_READ_CONST_CHK(resources[i - 1].output, opcode, CREATE) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, DELETE) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, MODIFY) ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, QUERY)  ||
					  _READ_CONST_CHK(resources[i - 1].output, opcode, CONTENT) || 
					  _READ_CONST_CHK(resources[i - 1].output, opcode, EXISTS)))
			return ERROR_CODE(int);

		if(ERROR_CODE(pstd_type_accessor_t) == (res->opcode_acc =pstd_type_model_get_accessor(ctx->model, res->output, "opcode")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.opcode", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->parent_id_acc =pstd_type_model_get_accessor(ctx->model, res->output, "parent_id")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.parent_id", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->object_id_acc =pstd_type_model_get_accessor(ctx->model, res->output, "object_id")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.object_id", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->param_acc =pstd_type_model_get_accessor(ctx->model, res->output, "param.token")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.param", ctx->resources[i - 1].res_name);
		if(ERROR_CODE(pstd_type_accessor_t) == (res->content_acc =pstd_type_model_get_accessor(ctx->model, res->output, "content.token")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for %s.param", ctx->resources[i - 1].res_name);
	}

	qsort(ctx->resources, ctx->count, sizeof(resource_ctx_t), _cmp);

	for(i = 0; i < ctx->count; i ++)
	{
		if(ctx->resources[i].parent_name == NULL) continue;
		for(j = 0; j < ctx->count; j ++)
			if(strcmp(ctx->resources[i].parent_name, ctx->resources[j].res_name) == 0)
			{
				if(i == j) ERROR_RETURN_LOG(int, "Self referencing is not allowed");
				ctx->resources[i].parent = ctx->resources + j;
				break;
			}
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

	const pstd_string_t* ps = pstd_string_from_rls(token);

	if(ps == NULL) return NULL;

	return pstd_string_value(ps);
}

/**
 * @brief search for the first element bound of the element which is strictly larger than ch
 **/
static inline uint32_t _search(const context_t* ctx, char ch, uint32_t n, uint32_t l, uint32_t r)
{
	/* If all the elements is larger than ch, which means the bound should be [l,l) */
	if(ctx->resources[l].res_name[n] > ch)
		return l;

	/* Search for the last item which ctx->resources[x]->res_name[n] <= ch */
	while(r - l > 1)
	{
		uint32_t m = (l + r) / 2;
		if(ctx->resources[m].res_name[n] <= ch) l = m;
		else r = m;
	}

	return r;
}

static inline object_id_t* _parse_object_id(char const** path, object_id_t* buf)
{
	const char* begin = *path;
	/* Step1: strip the leading slash */
	while(*begin == '/') begin ++;

	/* Step2: delete the object id */
	if(*begin == '$')
	{
		begin ++;
		const char* end = begin;
		while(*end && *end != '/') end ++;
		if(sizeof(*buf) == bsr64_to_bin(begin, end, buf->u8, sizeof(*buf)))
			return NULL;
		*path = end;
		return buf;
	}

	return NULL;
}

static inline const resource_ctx_t* _parse_resource_type(char const* * path, const context_t* ctx)
{
	const char* begin = *path;
	while(*begin == '/') begin ++;
	if(*begin == '/') begin ++;
	uint32_t l = 0, r = ctx->count, i;
	/* Do the binary search */
	for(i = 0;*begin != 0 && *begin != '/' && r - l > 1; begin ++, i ++)
	{
		char ch = *begin;
		l =_search(ctx, (char)(ch - 1), i, l, r);
		if(r - l < 1 || ctx->resources[l].res_name[i] != begin[0]) 
			return NULL;
		r = _search(ctx, ch, i, l, r);
		if(r - l < 1 || ctx->resources[r - 1].res_name[i] != begin[0])
			return NULL;
	}
	for(;*begin != 0 && *begin != '/'; begin ++, i ++)
		if(ctx->resources[l].res_name[i] != *begin) 
			return NULL;

	*path = begin;

	return ctx->resources + l;
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
	if(NULL == path) goto EXIT_NORMALLY;

	/* we need to detect the parent id */
	object_id_t parent_id_buf = {};
	object_id_t* parent_id = _parse_object_id(&path, &parent_id_buf);

	/* Then we need to parse the resource type */
	const resource_ctx_t* res_ctx = _parse_resource_type(&path, ctx);
	if(NULL == res_ctx) goto EXIT_NORMALLY; /* This means we can not handle this request */

	/* Parse the object id */
	object_id_t object_id_buf = {};
	object_id_t* object_id = _parse_object_id(&path, &object_id_buf);

	uint32_t method = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, inst, ctx->method_acc);
	if(ERROR_CODE(uint32_t) == method) ERROR_LOG_GOTO(EXIT, "Cannot read method code from the request input");
	
	scope_token_t param_token, data_token;
	if(ERROR_CODE(scope_token_t) == (param_token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->param_acc)))
		ERROR_LOG_GOTO(EXIT, "Cannot read the RLS token for param");

	if(ERROR_CODE(scope_token_t) == (data_token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->data_acc)))
		ERROR_LOG_GOTO(EXIT, "Cannot read the RLS token for data"); 

	uint32_t storage_opcode = ERROR_CODE(uint32_t);
	
	if(method == ctx->method_code.POST)
	{
		if(object_id == NULL)
		{
			/* This means we want to create a new resource */
			storage_opcode = ctx->opcode.CREATE;
			object_id = &object_id_buf;
			if((parent_id == NULL) ^ (res_ctx->parent == NULL))
			{
				LOG_DEBUG("Creating a isolated resource which should have a parent or a attached resource which should be isolated"
						  "is perhibited");
				goto EXIT_NORMALLY;
						  
			}
			uuid_generate(object_id_buf.uuid);
		}
		else 
		{
			/* This means we want to modify an existing resource */
			storage_opcode = ctx->opcode.MODIFY;
			parent_id = NULL;  /* we ignore the parent id in this case */
		}

	}
	else if(method == ctx->method_code.DELETE)
	{
		if(object_id != NULL)
		{
			storage_opcode = ctx->opcode.DELETE;
			parent_id = NULL;    /* we ignore the parent id */
		}
	}
	else if(method == ctx->method_code.GET)
	{
		if(object_id == NULL)
		{
			storage_opcode = ctx->opcode.QUERY;
		}
		else
		{
			storage_opcode = ctx->opcode.CONTENT;
			parent_id = NULL;   /* we ignore the parent id */
		}
	}

	/* If this happens, it means it's not a valid restful operation */
	if(ERROR_CODE(uint32_t) == storage_opcode) goto EXIT_NORMALLY;

	/* Construct the output */
	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, res_ctx->opcode_acc, storage_opcode))
		ERROR_LOG_GOTO(EXIT, "Cannot write the storage opcode to the output pipe");

	if(parent_id != NULL && ERROR_CODE(int) == pstd_type_instance_write(inst, res_ctx->parent_id_acc, parent_id->u8, sizeof(parent_id->u8)))
		ERROR_LOG_GOTO(EXIT, "Cannot write the parent id to the storage command output");

	if(object_id != NULL && ERROR_CODE(int) == pstd_type_instance_write(inst, res_ctx->object_id_acc, object_id->u8, sizeof(object_id->u8)))
		ERROR_LOG_GOTO(EXIT, "Cannot write the object id to the storage command output");

	/* Then we need to copy the parameters and data */

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, res_ctx->content_acc, data_token))
		ERROR_LOG_GOTO(EXIT, "Cannot write the data RLS token to the storage command");

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, res_ctx->param_acc, param_token))
		ERROR_LOG_GOTO(EXIT, "Cannot write the param RLS token to the storage command");

	/* If the URL contains a parent id, then we need to ask the parent storage controller if the parent exists */
	if(parent_id != NULL)
	{
		/* If the resource context do not have a parent resource, things goes wrong */ 
		if(res_ctx->parent == NULL) goto EXIT_NORMALLY;
		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, res_ctx->parent->opcode_acc, ctx->opcode.EXISTS))
			ERROR_LOG_GOTO(EXIT, "Cannot write the exists validation opcode to the parent stroage controller");

		if(ERROR_CODE(int) == pstd_type_instance_write(inst, res_ctx->parent->object_id_acc, parent_id->u8, sizeof(parent_id->u8)))
			ERROR_LOG_GOTO(EXIT, "Cannot write the parent object id to the storage controller");
	}


EXIT_NORMALLY:
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
