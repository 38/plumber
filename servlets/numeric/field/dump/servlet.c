/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <pservlet.h>
#include <pstd.h>

#include <psnl.h>

#include <options.h>

typedef struct {
	options_t              options;   /*!< The servlet options */

	pipe_t                 p_field;   /*!< The pipe for the field */
	pipe_t                 p_dump;    /*!< The dump of the field */

	pstd_type_model_t*     type_model;/*!< The servlet type model */

	pstd_type_accessor_t   a_field_tok; /*!< The accessor for the field token */

	psnl_cpu_field_type_info_t field_type;  /*!< The field type description */
} ctx_t;

static int _field_type_assert(pipe_t pipe, const char* typename, void* data)
{
	(void)pipe;

	ctx_t* ctx = (ctx_t*)data;

	if(ERROR_CODE(int) == psnl_cpu_field_type_parse(typename, &ctx->field_type))
		ERROR_RETURN_LOG(int, "Cannot parse the typename as a field type");

	return 0;
}

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int,  "Cannot parse the servlet init param");

	PIPE_LIST(pipes)
	{
		PIPE("field", PIPE_INPUT,  "$T",               ctx->p_field),
		PIPE("dump",  PIPE_OUTPUT, "plumber/base/Raw", ctx->p_dump) 
	};

	if(ERROR_CODE(int) == PIPE_BATCH_INIT(pipes))
		ERROR_RETURN_LOG(int, "Cannot initialize the pipe");

	PSTD_TYPE_MODEL(type_model)
	{
		PSTD_TYPE_MODEL_FIELD(ctx->p_field,     token,       ctx->a_field_tok)
	};

	if(NULL == (ctx->type_model = PSTD_TYPE_MODEL_BATCH_INIT(type_model)))
		ERROR_RETURN_LOG(int, "Cannot create the type model for this servlet");

	if(ERROR_CODE(int) == pstd_type_model_assert(ctx->type_model, ctx->p_field, _field_type_assert, ctx))
		ERROR_RETURN_LOG(int, "Cannot setup the type assertion callback");

	return 0;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;

	if(ERROR_CODE(int) == options_free(&ctx->options))
		rc = ERROR_CODE(int);

	if(NULL != ctx->type_model && ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);

	return rc;
}

static inline int _dim_data(pstd_bio_t* out, const ctx_t* ctx, int32_t val)
{
	if(ctx->options.binary)
	{
		size_t bytes_to_write = sizeof(val);

		for(;bytes_to_write;)
		{
			size_t bytes_written = pstd_bio_write(out, &val, bytes_to_write);

			if(ERROR_CODE(size_t) == bytes_written)
				ERROR_RETURN_LOG(int, "Cannot write data to the output");

			bytes_to_write -= bytes_written;
		}

		return 0;
	}

	if(ERROR_CODE(size_t) == pstd_bio_printf(out, "%d", val))
		ERROR_RETURN_LOG(int, "Cannot write the value");

	return 0;
}

static inline int _data(pstd_bio_t* out, const ctx_t* ctx, const void* data)
{
	if(ctx->options.binary)
	{
		size_t bytes_to_write = ctx->field_type.cell_size;

		for(;bytes_to_write;)
		{
			size_t bytes_written = pstd_bio_write(out, data, bytes_to_write);

			if(ERROR_CODE(size_t) == bytes_written)
				ERROR_RETURN_LOG(int, "Cannot write data to the output");

			bytes_to_write -= bytes_written;
		}

		return 0;
	}

	switch(ctx->field_type.cell_type)
	{
		case PSNL_CPU_FIELD_CELL_TYPE_DOUBLE:
			if(ERROR_CODE(size_t) == pstd_bio_printf(out, "%lf", *(const double*)data))
				ERROR_RETURN_LOG(int, "Cannot write the data to output");
			break;
		default:
			ERROR_RETURN_LOG(int, "Invalid type code");
	}

	return 0;
}

static inline int _field_sep(pstd_bio_t* out, const ctx_t* ctx)
{
	if(ctx->options.binary) return 0;

	if(ERROR_CODE(int) == pstd_bio_putc(out, ' '))
		ERROR_RETURN_LOG(int, "Cannot write the file seperator");

	return 0;
}

static inline int _new_line(pstd_bio_t* out, const ctx_t* ctx)
{
	if(ctx->options.binary) return 0;

	if(ERROR_CODE(int) == pstd_bio_putc(out, '\n'))
		ERROR_RETURN_LOG(int, "Cannot write the new line");
	
	return 0;
}

static inline int _new_slice(pstd_bio_t* out, const ctx_t* ctx)
{
	if(ctx->options.binary) return 0;

	if(ERROR_CODE(size_t) == pstd_bio_puts(out, "\n\n"))
		ERROR_RETURN_LOG(int, "Cannot write the slice sperator");
	
	return 0;
}

static inline int _dump_data(pstd_bio_t* out, const ctx_t* ctx, const void* data, int32_t* pos, const psnl_dim_t* dim, uint32_t n)
{
	uint32_t dim_rem = dim->n_dim - n;

	if(dim_rem == 0) return 0;

	if(dim_rem == 1)
	{
		uint32_t j = 0;
		for(pos[n] = dim->dims[n][0]; pos[n] < dim->dims[n][1]; pos[n] ++, j ++)
		{
			if(ERROR_CODE(int) == _data(out, ctx, ((const int8_t*)data) + (ctx->field_type.cell_size * psnl_dim_get_offset(dim, pos))))
				return ERROR_CODE(int);

			if(pos[n] == dim->dims[n][1] - 1)
			{
				if(ERROR_CODE(int) == _new_line(out, ctx))
					return ERROR_CODE(int);
			}
			else
			{
				if(ERROR_CODE(int) == _field_sep(out, ctx))
					return ERROR_CODE(int);
			}
		}

		return 0;
	}

	if(dim_rem == 2)
	{
		if(ERROR_CODE(int) == _new_slice(out, ctx))
			return ERROR_CODE(int);

		if(ctx->options.slice_coord)
		{
			uint32_t i;
			for(i = 0; i + 2 < dim->n_dim; i ++)
			{
				if(i > 0 && ERROR_CODE(int) == _field_sep(out, ctx))
					return ERROR_CODE(int);

				if(ERROR_CODE(int) == _dim_data(out, ctx, pos[i]))
					return ERROR_CODE(int);
			}

			if(ERROR_CODE(int) == _new_line(out, ctx))
				return ERROR_CODE(int);
		}

		for(pos[n] = dim->dims[n][0]; pos[n] < dim->dims[n][1]; pos[n] ++)
		{

			if(ERROR_CODE(int) == _dump_data(out, ctx, data, pos, dim, n + 1))
				return ERROR_CODE(int);
		}

		return 0;
	}

	for(pos[n] = dim->dims[n][0]; pos[n] < dim->dims[n][1]; pos[n] ++)
		if(ERROR_CODE(int) == _dump_data(out, ctx, data, pos, dim, n + 1))
			return ERROR_CODE(int);

	return 0;
}

static int _exec(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	pstd_bio_t* out = NULL;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	scope_token_t tok;
	
	const psnl_cpu_field_t* field;
	
	const psnl_dim_t* dim = NULL;

	const void* data;

	if(NULL == inst)
		ERROR_RETURN_LOG(int,  "Cannot create the type instance");

	if(NULL == (out = pstd_bio_new(ctx->p_dump)))
		ERROR_LOG_GOTO(ERR, "Cannot create the BIO object");

	if(ERROR_CODE(scope_token_t) == (tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->a_field_tok)))
		ERROR_LOG_GOTO(ERR, "Cannot read the scope token");

	if(NULL == (field = psnl_cpu_field_from_rls(tok)))
		ERROR_LOG_GOTO(ERR, "Cannot acquire the RLS object from the token");

	if(NULL == (data = psnl_cpu_field_get_data_const(field, &dim)))
		ERROR_LOG_GOTO(ERR, "Cannot get the field data");

	/* If we need to dump the dimension data */
	if(ctx->options.dump_dim)
	{
		if(ERROR_CODE(int) == _dim_data(out, ctx, (int32_t)dim->n_dim) ||
		   ERROR_CODE(int) == _new_line(out, ctx))
			ERROR_RETURN_LOG(int, "Cannot write the dimension data");

		uint32_t i;
		for(i = 0; i < dim->n_dim; i ++)
		{
			if(ERROR_CODE(int) == _dim_data(out, ctx, dim->dims[i][0]) ||
			   ERROR_CODE(int) == _field_sep(out, ctx) ||
			   ERROR_CODE(int) == _dim_data(out, ctx, dim->dims[i][1]))
				ERROR_RETURN_LOG(int, "Cannot write the dimension range");
			if(i + 1 !=  dim->n_dim && ERROR_CODE(int) == _field_sep(out, ctx))
				ERROR_RETURN_LOG(int, "Cannot write the field seperator");
		}
	}

	/* Write the actual data */
	{

		int32_t pos[dim->n_dim];

		if(ERROR_CODE(int) == _dump_data(out, ctx, data, pos, dim, 0))
			ERROR_LOG_GOTO(ERR, "Cannot dump the data body");
	}

	if(ERROR_CODE(int) == pstd_bio_free(out))
		ERROR_LOG_GOTO(ERR, "Cannot dispose the BIO object");

	if(ERROR_CODE(int) == pstd_type_instance_free(inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	return 0;

ERR:
	if(NULL != inst) pstd_type_instance_free(inst);
	if(NULL != out)  pstd_bio_free(out);
	return ERROR_CODE(int);
}

SERVLET_DEF = {
	.desc = "Dump the field to either raw pipe or a RLS string object",
	.version = 0x0,
	.size = sizeof(ctx_t),
	.init = _init,
	.unload = _unload,
	.exec = _exec
};
