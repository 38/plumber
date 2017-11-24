/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <rapidjson/memorybuffer.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/writer.h>
#include <rapidjson/document.h>

#include <utils/static_assertion.h>

#include <pstd.h>
#include <pstd/types/string.h>
#include <pservlet.h>
#include <proto.h>
#include <module/simulate/api.h>


#include <json_model.h>

/**
 * @brief The thread local used by each worker thread
 **/
typedef struct {
	size_t   size;  /*!< The size of the buffer */
	char*    buf;   /*!< The actual buffer */
} tl_buf_t;

/**
 * @brief Indicates how many times the init function has been called
 **/
static int _init_count;
/**
 * @brief The shared thread locals
 **/
static pstd_thread_local_t* _tl_bufs;

/**
 * @brief The servlet context
 **/
typedef struct {
	uint32_t      from_json:1;      /*!< Indicates if we want json to typed pipes */
	uint32_t      raw:1;            /*!< Indicates if this servlet takes raw input */
	pipe_t        json;             /*!< The pipe we input JSON string */
	uint32_t      count;            /*!< The numer of typed ports */
	json_model_t* typed;            /*!< The typed pipes */
	pstd_type_model_t*   model;     /*!< The type model */
	pstd_type_accessor_t json_acc;  /*!< The input accessor */
} context_t;

static void* _tl_buf_alloc(uint32_t tid, const void* data)
{
	(void)tid;
	(void)data;
	tl_buf_t* ret = (tl_buf_t*)malloc(sizeof(*ret));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate mmory for the thead local buffer");
	ret->size = 4096;
	if(NULL == (ret->buf = (char*)malloc(ret->size)))
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the buffer memory");
	return ret;
}

static int _tl_buf_dealloc(void* mem, const void* data)
{
	(void)data;
	tl_buf_t* ret = (tl_buf_t*)mem;
	if(ret->buf != NULL) free(ret->buf);
	free(ret);
	return 0;
}

static inline int _tl_buf_resize(tl_buf_t* mem)
{
	char* new_mem = (char*)realloc(mem->buf, mem->size * 2);
	if(NULL == new_mem)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the buffer to size %zu", mem->size * 2);

	mem->size *= 2;
	mem->buf = new_mem;

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
#ifdef LOG_ERROR_ENABLED
	const char* servlet_name = argv[0];
#endif

	context_t* ctx = (context_t*)ctxbuf;

	ctx->raw = 0;
	ctx->from_json = 1u;

	uint32_t i;
	for(i = 0; i < 2u && argc > 1u; i ++)
	{
		if(strcmp(argv[1], "--raw") == 0)
		    argc --, argv ++, ctx->raw = 1u;
		else if(strcmp(argv[1], "--from-json") == 0)
		    argc --, argv ++;
		else if(strcmp(argv[1], "--to-json") == 0)
		    argc --, argv ++, ctx->from_json = 0;
		else break;
	}

	ctx->typed = NULL;
	ctx->model = NULL;

	if(argc < 2)
	    ERROR_RETURN_LOG(int, "Usage: %s [--from-json|--to-json] [--raw] <name>:<type> [<name>:<type> ...]", servlet_name);

	ctx->count = argc - 1;
	if(NULL == (ctx->typed = (json_model_t*)calloc(ctx->count, sizeof(ctx->typed[0]))))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the outputs");

	if(NULL == (ctx->model = pstd_type_model_new()))
	    ERROR_RETURN_LOG(int, "Cannot create new type model for the servlet");

	if(ERROR_CODE(pipe_t) == (ctx->json = pipe_define("json", ctx->from_json ? PIPE_INPUT : PIPE_OUTPUT,
	                                                  ctx->raw ? "plumber/base/Raw" : "plumber/std/request_local/String")))
	    ERROR_RETURN_LOG(int, "Cannot define pipe for the JSON input");

	if(ERROR_CODE(int) == proto_init())
	    ERROR_RETURN_LOG(int, "Cannot intialize libproto");

	for(i = 0; i < ctx->count; i ++)
	{
		const char* arg = argv[i + 1];
		char pipe_name[128];
		uint32_t len = 0;
		for(;*arg != 0 && *arg != ':' && len < sizeof(pipe_name) - 1; pipe_name[len++] = *(arg++));
		if(*arg != ':') ERROR_LOG_GOTO(EXIT_PROTO, "Invalid output descriptor: %s", argv[i + 1]);
		pipe_name[len] = 0;
		const char* type = arg + 1;

		if(NULL == json_model_new(pipe_name, type, ctx->from_json ? 0 : 1, ctx->model, ctx->typed + i))
		    ERROR_LOG_GOTO(EXIT_PROTO, "Cannot initialize the JSON model for pipe %s", pipe_name);
	}

	goto EXIT_PROTO_NORMALLY;

EXIT_PROTO:
	proto_finalize();
	return ERROR_CODE(int);

EXIT_PROTO_NORMALLY:
	if(ERROR_CODE(int) == proto_finalize())
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot finalize libproto");

	if(ctx->raw)
	{
		if(ctx->from_json && _tl_bufs == NULL && NULL == (_tl_bufs = pstd_thread_local_new(_tl_buf_alloc, _tl_buf_dealloc, NULL)))
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot initailize the thread local");
	}
	else
	{
		if(ERROR_CODE(pstd_type_accessor_t) == (ctx->json_acc = pstd_type_model_get_accessor(ctx->model, ctx->json, "token")))
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot get the token accessor for the input json");
	}

	_init_count ++;

	return 0;
}

static int _cleanup(void* ctxbuf)
{
	int rc = 0;
	context_t* ctx = (context_t*)ctxbuf;

	if(NULL != ctx->typed)
	{
		uint32_t i;
		for(i = 0; i < ctx->count; i ++)
		    if(ERROR_CODE(int) == json_model_free(ctx->typed + i))
		        rc = ERROR_CODE(int);
		free(ctx->typed);
	}

	if(NULL != ctx->model && ERROR_CODE(int) == pstd_type_model_free(ctx->model))
	    rc = ERROR_CODE(int);

	if(0 == --_init_count && NULL != _tl_bufs && ERROR_CODE(int) == pstd_thread_local_free(_tl_bufs))
	    rc = ERROR_CODE(int);

	return rc;
}

static inline int _write(pstd_string_t* str, pstd_bio_t* bio, const char* fmt, ...)
{
	if(NULL == fmt) return 0;
	va_list ap;
	va_start(ap, fmt);
	if(bio != NULL)
	{
		if(ERROR_CODE(size_t) == pstd_bio_vprintf(bio, fmt, ap))
		    ERROR_RETURN_LOG(int, "Cannot write content to pipe");
	}
	else
	{
		if(ERROR_CODE(size_t) == pstd_string_vprintf(str, fmt, ap))
		    ERROR_RETURN_LOG(int, "Cannot write content to string");
	}
	va_end(ap);

	return 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-overflow"
static inline int _write_name(pstd_string_t* str, pstd_bio_t* bio, const char* fmt, const char* name)
{
	rapidjson::MemoryBuffer stream;
	rapidjson::Writer<rapidjson::MemoryBuffer> writer(stream);
	if(!writer.String(name)) ERROR_RETURN_LOG(int, "Cannot create JSON object for the pipe name %s", name);
	stream.Put(0);
	const char* name_repr = stream.GetBuffer();
	if(NULL == name_repr) ERROR_RETURN_LOG(int, "Cannot get the JSON representation of the pipe name %s", name);
	if(ERROR_CODE(int) == _write(str, bio, fmt, name_repr))
	    ERROR_RETURN_LOG(int, "Cannot write JSON string to the pipe");
	return 0;
}
#pragma GCC diagnostic pop

static inline int _exec_to_json(context_t* ctx, pstd_type_instance_t* inst)
{
	pstd_string_t* str = NULL;
	pstd_bio_t*    bio = NULL;
	uint32_t i, first = 1;

	if(ctx->raw && NULL == (bio = pstd_bio_new(ctx->json)))
	    ERROR_LOG_GOTO(ERR, "Cannot create new BIO object on the json pipe");

	if(!ctx->raw && NULL == (str = pstd_string_new(32)))
	    ERROR_LOG_GOTO(ERR, "Cannot create new string object for the JSON content");

	_write(str, bio, "{");

	for(i = 0; i < ctx->count; i ++)
	{
		const json_model_t* jm = ctx->typed + i;

		int eof_rc = pipe_eof(jm->pipe);
		if(ERROR_CODE(int) == eof_rc) ERROR_RETURN_LOG(int, "Cannot check if the pipe contnains no data");
		if(eof_rc) continue;

		if(ERROR_CODE(int) == _write_name(str, bio, first ? "%s:" : ",%s:", jm->name))
		    ERROR_LOG_GOTO(ERR, "Cannot write the pipe name");

		first = 0;

		uint32_t pc;
		enum {
			_O1,
			_O2,
			_C1,
			_C2
		} state = _O1;
		const char* stack[1024];
		uint32_t sp = 0;
		for(pc = 0; pc < jm->nops; pc ++)
		{
			if(sp >= sizeof(stack)/sizeof(stack[0]))
			    ERROR_LOG_GOTO(ERR, "Operation stack overflow");

			const json_model_op_t* op = jm->ops + pc;
			if(op->opcode == JSON_MODEL_OPCODE_OPEN || op->opcode == JSON_MODEL_OPCODE_OPEN_SUBS)
			{
				if(state == _O1 || state == _O2) state = _O2;
				else state = _O1;
			}
			else if(op->opcode == JSON_MODEL_OPCODE_CLOSE)
			{
				if(state == _C1 || state == _C2) state = _C2;
				else state = _C1;
			}
			switch(op->opcode)
			{
				case JSON_MODEL_OPCODE_OPEN:
				    if(ERROR_CODE(int) == _write_name(str, bio, state == _O2 ? "{%s:" : ",%s:", op->field))
				        ERROR_LOG_GOTO(ERR, "Cannot write field name");
				    if(state == _O2) stack[sp++] = "}";
				    break;
				case JSON_MODEL_OPCODE_OPEN_SUBS:
				    if(ERROR_CODE(int) == _write(str, bio, state == _O2 ? "[" : ","))
				        ERROR_LOG_GOTO(ERR, "Cannot write the list sperator");
				    if(state == _O2) stack[sp++] = "]";
				    break;
				case JSON_MODEL_OPCODE_CLOSE:
				    if(state == _C2 && (sp == 0 || ERROR_CODE(int) == _write(str, bio, stack[--sp])))
				        ERROR_LOG_GOTO(ERR, "Cannote write the end of block");
				    break;
				case JSON_MODEL_OPCODE_WRITE:
				    switch(op->type)
				    {
					    case JSON_MODEL_TYPE_SIGNED:
					    {
						    int64_t val = 0;
						    if(ERROR_CODE(size_t) == pstd_type_instance_read(inst, op->acc, &val, op->size))
						        ERROR_LOG_GOTO(ERR, "Cannot read data from the typed pipe");

						    if(ERROR_CODE(int) == _write(str, bio, "%" PRId64, val))
						        ERROR_LOG_GOTO(ERR, "Cannot write the JSON value");
						    break;
					    }
					    case JSON_MODEL_TYPE_UNSIGNED:
					    {
						    uint64_t val = 0;
						    if(ERROR_CODE(size_t) == pstd_type_instance_read(inst, op->acc, &val, op->size))
						        ERROR_LOG_GOTO(ERR, "Cannot read data from the typed pipe");

						    if(ERROR_CODE(int) == _write(str, bio, "%" PRIu64, val))
						        ERROR_LOG_GOTO(ERR, "Cannot write the JSON value");
						    break;
					    }
					    case JSON_MODEL_TYPE_FLOAT:
					    {
						    union {
							    double d;
							    float  f;
						    } val;
						    if(ERROR_CODE(size_t) == pstd_type_instance_read(inst, op->acc, &val, op->size))
						        ERROR_LOG_GOTO(ERR, "Cannot read data from the typed pipe");

						    if(sizeof(double) == op->size)
						    {
							    if(ERROR_CODE(int) == _write(str, bio, "%llg", val.d))
							        ERROR_LOG_GOTO(ERR, "Cannot write the JSON value");
						    }
						    else if(ERROR_CODE(int) == _write(str, bio, "%g", val.f))
						        ERROR_LOG_GOTO(ERR, "Cannot write the JSON value");
						    break;
					    }
					    case JSON_MODEL_TYPE_STRING:
					    {
						    scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, op->acc);
						    if(ERROR_CODE(scope_token_t) == token)
						        ERROR_LOG_GOTO(ERR, "Cannot read RLS token");
						    if(token != 0)
						    {
							    const pstd_string_t* ps = pstd_string_from_rls(token);
							    if(NULL == ps) ERROR_LOG_GOTO(ERR, "Cannot get the RLS token from the Scope");
							    const char* val = pstd_string_value(ps);
							    if(NULL == val) ERROR_LOG_GOTO(ERR, "Cannot get the string from the RLS string object");
							    if(ERROR_CODE(int) == _write_name(str, bio, "%s", val))
							        ERROR_LOG_GOTO(ERR, "Cannot write the string to JSON represetnation");
						    }
						    else
						    {
							    if(ERROR_CODE(int) == _write(str, bio, "null"))
							        ERROR_LOG_GOTO(ERR, "Cannot write to JSON");
						    }
						    break;
					    }
				    }
			}
		}

		if(jm->nops == 0) _write(str, bio, "null");

		if(sp == 1) _write(str, bio, stack[0]);
	}
	_write(str, bio, "}");

	if(NULL != bio && ERROR_CODE(int) == pstd_bio_free(bio))
	    ERROR_RETURN_LOG(int, "Cannot dispose the BIO object");
	if(NULL != str)
	{
		scope_token_t token = pstd_string_commit(str);
		if(ERROR_CODE(scope_token_t) == token)
		{
			pstd_string_free(str);
			ERROR_RETURN_LOG(int, "Cannot commit the string to RLS");
		}
		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->json_acc, token))
		    ERROR_RETURN_LOG(int, "Cannot write token to the pipe");
	}
	return 0;
ERR:
	if(NULL != bio) pstd_bio_free(bio);
	if(NULL != str) pstd_string_free(str);
	return ERROR_CODE(int);
}

static inline int _exec_from_json(context_t* ctx, pstd_type_instance_t* inst)
{
	rapidjson::Document document;

	const char* data = NULL;
	size_t data_len = 0;

	if(ctx->raw)
	{
#ifdef LOG_INFO_ENABLED
		const char* input_label = NULL;

		if(ERROR_CODE(int) == pipe_cntl(ctx->json, MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL, &input_label))
		ERROR_RETURN_LOG(int, "Cannot get the input label");

		if(NULL != input_label) LOG_INFO("Processing event with label %s", input_label);
#endif
		/* If this servlet is in the raw mode, then we need to read it from the pipe directly */
		tl_buf_t* tl_buf = (tl_buf_t*)pstd_thread_local_get(_tl_bufs);
		if(NULL == tl_buf)
		    ERROR_RETURN_LOG(int, "Cannot get buffer memory from the thread local");
		size_t len = 0;
		for(;;)
		{
			int rc = pipe_eof(ctx->json);
			if(ERROR_CODE(int) == rc)
			    ERROR_RETURN_LOG(int, "Cannot check if there's more data in the json pipe");

			if(rc) break;

			size_t bytes_read = pipe_read(ctx->json, tl_buf->buf, tl_buf->size - len);
			if(ERROR_CODE(size_t) == bytes_read)
			    ERROR_RETURN_LOG(int, "Cannot read data from buffer");

			len += bytes_read;
			if(len + 1 >= tl_buf->size && ERROR_CODE(int) == _tl_buf_resize(tl_buf))
			    ERROR_RETURN_LOG(int, "Cannot resize the buffer");
		}
		data = tl_buf->buf;
		tl_buf->buf[data_len = len] = 0;
	}
	else
	{
		/* If this data comes from the RLS, we need to read the token */
		scope_token_t token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->json_acc);
		if(ERROR_CODE(scope_token_t) == token)
		    ERROR_RETURN_LOG(int, "Cannot read the token from the json pipe");
		if(0 == token)
		{
			data = "";
		}
		else
		{
			const pstd_string_t* str = pstd_string_from_rls(token);
			if(NULL == str || NULL == (data = pstd_string_value(str)))
			    ERROR_RETURN_LOG(int, "Cannot get the string from the given RLS token");
			if(ERROR_CODE(size_t) == (data_len = pstd_string_length(str)))
			    ERROR_RETURN_LOG(int, "Cannot get the length of the RLS string");
		}
	}

	if(NULL == data) ERROR_RETURN_LOG(int, "Cannot read data from input pipe");

	rapidjson::MemoryStream ims(data, data_len);

	/* Then we can parse the JSON string */
	if(document.ParseStream(ims).HasParseError())
	{
		LOG_DEBUG("Got Invalid JSON, exiting");
		return 0;
	}

	uint32_t i;
	for(i = 0; i < ctx->count; i ++)
	{
		const json_model_t* jmodel = ctx->typed + i;
		if(!document.HasMember(jmodel->name)) continue;
		rapidjson::Value& root = document[jmodel->name];

		rapidjson::Value* stack[1024];
		uint32_t sp = 1, pc = 0;
		stack[0] = &root;
		for(pc = 0; pc < jmodel->nops; pc ++)
		{
			if(sp == 0) ERROR_RETURN_LOG(int, "Invlid stack opeartion");
			rapidjson::Value* cur_obj = stack[sp - 1];
			const json_model_op_t* op = jmodel->ops + pc;
			switch(op->opcode)
			{
				case JSON_MODEL_OPCODE_OPEN:
				    if(sp >= sizeof(stack)) ERROR_RETURN_LOG(int, "Operation stack overflow");
				    if(cur_obj != NULL)
				    {
					    stack[sp] = NULL;
					    if(cur_obj->IsObject() && cur_obj->HasMember(op->field))
					        stack[sp] = &(*cur_obj)[op->field];
					    else
					        LOG_NOTICE("Missing field %s", op->field);
				    }
				    else stack[sp] = NULL;
				    sp ++;
				    break;
				case JSON_MODEL_OPCODE_OPEN_SUBS:
				    if(sp >= sizeof(stack)) ERROR_RETURN_LOG(int, "Operation stack overflow");
				    if(cur_obj != NULL)
				    {
					    if(!cur_obj->IsArray() || op->index >= cur_obj->Size())
					        LOG_NOTICE("Missing subscript %u", op->index);
					    else
					        stack[sp] = &(*cur_obj)[op->index];
				    }
				    else stack[sp] = NULL;
				    sp ++;
				    break;
				case JSON_MODEL_OPCODE_CLOSE:
				    sp --;
				    break;
				case JSON_MODEL_OPCODE_WRITE:
				    if(cur_obj == NULL) break;
				    switch(op->type)
				    {
					    case JSON_MODEL_TYPE_SIGNED:
					    case JSON_MODEL_TYPE_UNSIGNED:
					    {

						    int64_t value = 0;

						    if(!cur_obj->IsInt64())
						        LOG_NOTICE("Missing integer field, using default 0");
						    else
						        value = cur_obj->GetInt64();

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#	error("This doesn't work with big endian archtechture")
#endif
						    /* In this case we must expand the sign bit */
						    if(op->type == JSON_MODEL_TYPE_SIGNED && value < 0)
						        value |= ~((1ll << (8 * op->size - 1)) - 1);

						    if(ERROR_CODE(int) == pstd_type_instance_write(inst, op->acc, &value, op->size))
						        ERROR_RETURN_LOG(int, "Cannot write field");
						    break;
					    }
					    case JSON_MODEL_TYPE_FLOAT:
					    {
						    double d_value = 0;
						    if(!cur_obj->IsDouble())
						        LOG_NOTICE("Missing double field, using default 0");
						    else
						        d_value = cur_obj->GetDouble();
						    float  f_value = (float)d_value;
						    void* data_to_write = op->size == sizeof(double) ? (void*)&d_value : (void*)&f_value;
						    if(ERROR_CODE(int) == pstd_type_instance_write(inst, op->acc, data_to_write, op->size))
						        ERROR_RETURN_LOG(int, "Cannot write field");
						    break;
					    }
					    case JSON_MODEL_TYPE_STRING:
					    {
						    const char* str = "(null)";
						    if(!cur_obj->IsString())
						        LOG_NOTICE("Missing string field, using default (null)");
						    else
						        str = cur_obj->GetString();
						    if(NULL == str) ERROR_RETURN_LOG(int, "Cannot get the string value");
						    size_t len = strlen(str);
						    pstd_string_t* pstd_str = pstd_string_new(len + 1);
						    if(NULL == pstd_str) ERROR_RETURN_LOG(int, "Cannot allocate new pstd string object");
						    if(ERROR_CODE(size_t) == pstd_string_write(pstd_str, str, len))
						    {
							    pstd_string_free(pstd_str);
							    ERROR_RETURN_LOG(int, "Cannot write string to the pstd string object");
						    }

						    scope_token_t token = pstd_string_commit(pstd_str);
						    if(ERROR_CODE(scope_token_t) == token)
						    {
							    pstd_string_free(pstd_str);
							    ERROR_RETURN_LOG(int, "Cannot commit the string to the RLS");
						    }
						    /* From this point, we lose the ownership of the RLS object */
						    if(ERROR_CODE(int) == pstd_type_instance_write(inst, op->acc, &token, sizeof(scope_token_t)))
						        ERROR_RETURN_LOG(int, "Cannot write the RLS token to the output pipe");
						    break;
					    }
				    }
			}
		}
	}

	return 0;
}

static inline int _exec(void* ctxbuf)
{
	int rc = 0;
	context_t* ctx = (context_t*)ctxbuf;

	size_t ti_size = pstd_type_instance_size(ctx->model);
	if(ERROR_CODE(size_t) == ti_size)
	    ERROR_RETURN_LOG(int, "Cannot get the size of the type model");
	char ti_buf[ti_size];

	pstd_type_instance_t* inst = pstd_type_instance_new(ctx->model, ti_buf);
	if(NULL == inst) ERROR_RETURN_LOG(int, "Cannot create new type instance");

	if(ctx->from_json)
	    rc = _exec_from_json(ctx, inst);
	else
	    rc = _exec_to_json(ctx, inst);

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
	    ERROR_RETURN_LOG(int, "Cannot dispose the type instance");
	return rc;
}


PSERVLET_EXPORT(context_t, _init, _exec, _cleanup, "Parse the JSON to expected type", 0);
