/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>
#include <psnl.h>

#include <pstd/types/string.h>

#include <options.h>
#include <parser.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	pipe_t             p_in;           /*!< The input text */ 
	pipe_t             p_out;          /*!< The output field */
	pstd_type_model_t* type_model;     /*!< The type model for current servlet */
	pstd_type_accessor_t  a_in_tok;    /*!< The input token */
	pstd_type_accessor_t  a_out_tok;   /*!< The output token */
	options_t          options;        /*!< The servlet option */
} context_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	ctx->type_model = NULL;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the options");

	PIPE_LIST(pipes)
	{
		PIPE("input",     PIPE_INPUT,           ctx->options.input_type,          ctx->p_in),
		PIPE("output",    PIPE_OUTPUT,          ctx->options.result_type,         ctx->p_out)
	};
	
	if(ERROR_CODE(int) == PIPE_BATCH_INIT(pipes)) return ERROR_CODE(int);

	if(NULL == (ctx->type_model = pstd_type_model_new()))
		ERROR_RETURN_LOG(int, "Cannot allocate memory for the type model");

	if(!ctx->options.raw && ERROR_CODE(pstd_type_accessor_t) == (ctx->a_in_tok = pstd_type_model_get_accessor(ctx->type_model, ctx->p_in, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the input token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_out_tok = pstd_type_model_get_accessor(ctx->type_model, ctx->p_out, "token")))
		ERROR_RETURN_LOG(int, "Cannot get the output token");

	return 0;
}

static int _cleanup(void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	int rc = 0;

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == options_free(&ctx->options))
		rc = ERROR_CODE(int);

	return rc;
}

static int _assign_matrix(const psnl_dim_t* dim, size_t elem_size, const parser_request_t* req, uint32_t n, int32_t* pos, void* data)
{
	if(n == dim->n_dim) return parser_next_value(*req, (parser_result_buf_t)(void*)(((int8_t*)data) + psnl_dim_get_offset(dim, pos) * elem_size));

	for(pos[n] = dim->dims[n][0]; pos[n] < dim->dims[n][1]; pos[n] ++)
		if(ERROR_CODE(int) == _assign_matrix(dim, elem_size, req, n + 1, pos, data))
			return ERROR_CODE(int);

	return 0;
}

static int _exec(void* ctxmem)
{
	context_t* ctx = (context_t*)ctxmem;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == inst)
		ERROR_RETURN_LOG(int, "Cannot allocate the type instance");

	pstd_bio_t* p_in = NULL;
	const char* s_in = NULL, * s_end = NULL;

	if(ctx->options.raw && NULL == (p_in = pstd_bio_new(ctx->p_in)))
		ERROR_LOG_GOTO(ERR, "Cannot allocate the BIO for the input");

	if(!ctx->options.raw)
	{
		const pstd_string_t* rls_str = pstd_string_from_accessor(inst, ctx->a_in_tok);

		if(NULL == rls_str)
			ERROR_LOG_GOTO(ERR, "Cannot get the input string from RLS");

		if(NULL == (s_in = pstd_string_value(rls_str)))
			ERROR_LOG_GOTO(ERR, "Cannot get the value of the string");

		size_t sz_rc = pstd_string_length(rls_str);
		if(ERROR_CODE(size_t) == sz_rc)
			ERROR_LOG_GOTO(ERR, "Cannot get the length of the string");

		s_end = s_in + sz_rc;
	}
	
	psnl_dim_t* dim = NULL;

	parser_request_t pr;

	pr.repr = ctx->options.in_format == OPTIONS_INPUT_FORMAT_BINARY ? PARSER_REPR_BINARY : PARSER_REPR_LITERAL;

	if(ctx->options.raw)
	{
		pr.src_type = PARSER_SOURCE_TYPE_RAW_PIPE;
		pr.source.raw = p_in;
	}
	else
	{
		pr.src_type =  PARSER_SOURCE_TYPE_STR_BUF;
		pr.source.s_buf.begin = &s_in;
		pr.source.s_buf.end   = s_end;
	}

	if(NULL == ctx->options.dim_data)
	{
		/* TODO: Once we are able to parse int, we need to use that parser */
		double temp[2];
		pr.type = PARSER_VALUE_TYPE_DOUBLE;

		uint32_t i;
		if(NULL == (dim = PSNL_DIM_LOCAL_NEW_BUF(ctx->options.n_dim)))
			ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the new dimenional buffer");

		for(i = 0; i < ctx->options.n_dim; i ++)
		{
			if(ERROR_CODE(int) == parser_next_value(pr, (parser_result_buf_t)(temp + 0)) ||
			   ERROR_CODE(int) == parser_next_value(pr, (parser_result_buf_t)(temp + 1)))
				ERROR_LOG_GOTO(ERR, "Cannot parse the dimension");

			if(temp[0] >= temp[1])
				ERROR_LOG_GOTO(ERR, "Invalid dimension");

			dim->dims[i][0] = (int32_t)temp[0];
			dim->dims[i][1] = (int32_t)temp[1];
		}
	}

	/* TODO: the element size might be different */
	size_t elem_size = sizeof(double);

	psnl_cpu_field_t* field = psnl_cpu_field_new(dim, elem_size);

	if(NULL == field)
		ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the new field");

	do {

		int32_t pos[dim->n_dim];

		void* data = psnl_cpu_field_get_data(field, NULL);

		pr.type = PARSER_VALUE_TYPE_DOUBLE;
		if(NULL == data || ERROR_CODE(int) == _assign_matrix(dim, elem_size, &pr, 0, pos, data))
		{
			psnl_cpu_field_free(field);
			ERROR_LOG_GOTO(ERR, "Cannot assign matrix");
		}

	} while(0);

	scope_token_t tok = psnl_cpu_field_commit(field);
	if(ERROR_CODE(scope_token_t) == tok)
	{
		psnl_cpu_field_free(field);
		ERROR_LOG_GOTO(ERR, "Cannot commit the field to the token");
	}

	if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->a_out_tok, tok))
		ERROR_LOG_GOTO(ERR, "Cannot write the  token to the output pipe");
	else if(ERROR_CODE(int) == psnl_cpu_field_incref(field))
		ERROR_LOG_GOTO(ERR, "Cannot increase the refence counter");

	if(NULL != p_in && ERROR_CODE(int) == pstd_bio_free(p_in))
		ERROR_LOG_GOTO(ERR, "Cannot dispose the input BIO object");
	else
		p_in = NULL;

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
		ERROR_LOG_GOTO(ERR, "Cannot dispose the type instance");

	return 0;

ERR:
	if(NULL != p_in)
		pstd_bio_free(p_in);
	
	if(inst != NULL)
		pstd_type_instance_free(inst);

	return 0;
}

SERVLET_DEF = {
	.desc = "The parser to parse a initial field configuration",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.unload = _cleanup,
	.exec   = _exec
};
