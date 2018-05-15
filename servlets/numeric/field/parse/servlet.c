/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>
#include <psnl.h>

#include <pstd/types/string.h>

#include <options.h>

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

static int _read_next_raw_literal_double(pstd_bio_t* p_in, double* buf)
{
	*buf = 0;

	char sbuf[128];
	unsigned size = 0;
	
	char ch;

	for(;size < sizeof(sbuf) - 1;)
	{
		int getc_rc = pstd_bio_getc(p_in, &ch);

		if(getc_rc == ERROR_CODE(int)) 
			ERROR_RETURN_LOG(int, "Cannot get the next char");

		if(getc_rc == 0)
		{
			int eof_rc = pstd_bio_eof(p_in);
			if(ERROR_CODE(int) == eof_rc)
				ERROR_RETURN_LOG(int, "Cannot check the EOF of the input pipe");

			if(eof_rc) 
				break;

			/* TODO: do we really need to poll ? */
			continue;
		}

		if(size == 0 && (ch == '\t' || ch == ' ' || ch == '\r' || ch == '\n'))
			continue;

		if(!(ch >= '0' && ch <= '9') && 
		  !(ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-' || ch == 'x'))
			break;
		sbuf[size ++] = ch;
	}



	if(size == sizeof(sbuf))
		ERROR_RETURN_LOG(int, "Number is too long");

	sbuf[size] = 0;

	char* endptr = NULL;
	*buf = strtod(sbuf, &endptr);

	if(NULL == endptr || *endptr != 0)
		ERROR_RETURN_LOG(int, "Invalid number");

	return 0;
}

static int _read_next_string_literal_double(char const** data, const  char* end, double* buf)
{
	while((**data == '\t' || **data == ' ' || **data == '\r' || **data == '\n') && (*data) < end)
		(*data) ++;

	if(*data >= end) 
		ERROR_RETURN_LOG(int, "No more data");

	const char* start = *data;
	char* endptr;

	*buf = strtod(start, &endptr);

	if(NULL == endptr)
		ERROR_RETURN_LOG(int, "Invalid number");

	*data += (uintptr_t)endptr - (uintptr_t)*data;

	return 0;
}

static int _read_next_raw_binary_double(pstd_bio_t* bio, double* buf)
{
	size_t bytes_to_read = sizeof(double);
	void* start = buf;

	while(bytes_to_read > 0)
	{
		size_t rc = pstd_bio_read(bio, start, bytes_to_read);
		if(ERROR_CODE(size_t) == rc)
			ERROR_RETURN_LOG(int, "Cannot read bytes from the raw pipe");

		bytes_to_read -= rc;

		start = ((char*)start) + rc;
	}

	return 0;
}

static int _read_next_string_binary_double(char const** data, const char* end, double* buf)
{
	if(end < *data + sizeof(double))
		ERROR_RETURN_LOG(int, "No more data");

	union {
		const char*  s_data;
		const double* n_data;
	} view = {
		.s_data = *data
	};

	*buf = view.n_data[0];

	*data += sizeof(double);

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

	if(NULL == ctx->options.dim_data)
	{
		uint32_t i;
		if(NULL == (dim = PSNL_DIM_LOCAL_NEW_BUF(ctx->options.n_dim)))
			ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the new dimenional buffer");

		for(i = 0; i < ctx->options.n_dim; i ++)
		{
			double temp[2];

			if(ctx->options.in_format == OPTIONS_INPUT_FORMAT_BINARY && ctx->options.raw &&
			   (ERROR_CODE(int) == _read_next_raw_binary_double(p_in, temp + 0) ||
			    ERROR_CODE(int) == _read_next_raw_binary_double(p_in, temp+ 1)))
				/* TODO: Once we are able to parse int, we need to use that parser */
				ERROR_LOG_GOTO(ERR, "Cannot parse the dimension");

			if(ctx->options.in_format == OPTIONS_INPUT_FORMAT_STRING && ctx->options.raw &&
			   (ERROR_CODE(int) == _read_next_raw_literal_double(p_in, temp + 0) ||
			    ERROR_CODE(int) == _read_next_raw_literal_double(p_in, temp + 1)))
				ERROR_LOG_GOTO(ERR, "Cannot parse the dimension");

			if(ctx->options.in_format == OPTIONS_INPUT_FORMAT_BINARY && !ctx->options.raw &&
			   (ERROR_CODE(int) == _read_next_string_binary_double(&s_in, s_end, temp + 0) ||
			    ERROR_CODE(int) == _read_next_string_binary_double(&s_in, s_end, temp + 1)))
				ERROR_LOG_GOTO(ERR, "Cannot parse the dimension");

			if(ctx->options.in_format == OPTIONS_INPUT_FORMAT_STRING && !ctx->options.raw &&
			   (ERROR_CODE(int) == _read_next_string_literal_double(&s_in, s_end, temp + 0) ||
			    ERROR_CODE(int) == _read_next_string_literal_double(&s_in, s_end, temp + 1)))
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

	/* TODO: assign value to the field */

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
