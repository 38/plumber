/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <proto.h>

#include <log.h>
#include <lexer.h>
#include <compiler.h>

/**
 * @brief the internal context of the compiler
 **/
typedef struct {
	compiler_options_t options;      /*!< the options of the compiler */
	lexer_t*           lexer;        /*!< the lexer we used */
	compiler_result_t* result;       /*!< the compiler result */
	lexer_token_t*     lh_buffer[3]; /*!< the look ahead buffer */
	uint32_t           lh_begin;     /*!< where the look ahead buffer begins */
} _context_t;

/**
 * @brief peek the next n token in the compiler context
 * @note this function will peek and not consume the token until the consume is called
 * @param context the compiler context
 * @param n the look ahead number
 * @return the result lexer token, NULL if there's an error
 * @note this function won't validate pointers, be careful
 **/
static inline const lexer_token_t* _peek(_context_t* context, uint32_t n)
{
	uint32_t lh_limit = sizeof(context->lh_buffer) / sizeof(context->lh_buffer[0]);

	if(n > lh_limit || n == 0)
	    ERROR_PTR_RETURN_LOG("Invalid lookahead number %u", n);

	uint32_t i;
	for(i = 0; i < n; i ++)
	{
		uint32_t ofs = (context->lh_begin + i) % lh_limit;
		if(NULL == context->lh_buffer[ofs])
		{
			context->lh_buffer[ofs] = lexer_next_token(context->lexer);
			if(context->lh_buffer[ofs] == NULL)
			    ERROR_PTR_RETURN_LOG("error: Cannot get lexer token from lexer");
		}
	}

	return context->lh_buffer[(context->lh_begin + n - 1) % lh_limit];
}

/**
 * @brief consume the token that is already in lookahead buffer
 * @param context the compiler context
 * @param n now many tokens needs to be consumed
 * @return status code
 **/
static inline int _consume(_context_t* context, uint32_t n)
{
	uint32_t lh_limit = sizeof(context->lh_buffer) / sizeof(context->lh_buffer[0]);

	if(n > lh_limit)
	    ERROR_RETURN_LOG(int, "Cannot consume the token that is not in the lookahead buffer");

	uint32_t i;
	for(i = 0; i < n; i ++)
	{
		uint32_t ofs = (context->lh_begin + i) % lh_limit;
		if(NULL == context->lh_buffer[ofs])
		    ERROR_RETURN_LOG(int, "Cannot consume a token that is not yet in the lookahead buffer");

		if(ERROR_CODE(int) == lexer_token_free(context->lh_buffer[ofs]))
		    LOG_WARNING("Error during disposing the consumed token, memory leaked");

		context->lh_buffer[ofs] = NULL;
	}

	context->lh_begin = (context->lh_begin + n) % lh_limit;

	return 0;
}

/**
 * @brief raise an error, print out the error message and the error context information
 * @param ctx the error context
 * @param action the action after we raise the error
 * @param message the message to outout
 **/
#define _RAISE(ctx, action, message) do {\
	const lexer_token_t* token = _peek(ctx, 1);\
	if(token == NULL) \
	    LOG_ERROR("Cannot peek token");\
	else\
	    LOG_ERROR("%s:%d:%d: error: %s", token->file, token->line + 1, token->column + 1, message);\
	action;\
} while(0)

/**
 * @brief try to consume n tokens, if error happen, raise an internal error
 * @param ctx the context
 * @param action the action on error
 * @param n how many tokens to comsue
 **/
#define _TRY_CONSUME(ctx, action, n) do {\
	if(ERROR_CODE(int) == _consume(ctx, n))\
	    _RAISE(ctx, action, "Internal error: can not consume token");\
} while(0)

/**
 * @brief handle the libproto error
 * @param ctx the compiler context
 * @param action the next step
 **/
#ifdef LOG_ERROR_ENABLED
#	define _LIB_PROTO_ERROR(ctx, action) do {\
	    const proto_err_t* err = proto_err_stack();\
	    while(err != NULL) \
	    {\
		    static char buf[512];\
		    LOG_ERROR("libproto: %s", proto_err_str(err, buf, sizeof(buf)));\
		    err = err->child;\
	    }\
	    action;\
    } while(0)
#else
#	define _LIB_PROTO_ERROR(ctx, action) do {\
	    action;\
    } while(0)
#endif

/**
 * @brief parse a type reference
 * @param ctx context
 * @return the type reference
 **/
static inline proto_ref_typeref_t* _parse_type_ref(_context_t* ctx)
{
	proto_ref_typeref_t* ret;

	if(NULL == (ret = proto_ref_typeref_new(32)))  /* TODO: make this configurable */
	    _LIB_PROTO_ERROR(ctx, return NULL);

	const lexer_token_t* next_tok = _peek(ctx, 1);
	if(NULL == next_tok || next_tok->type != LEXER_TOKEN_ID)
	    _RAISE(ctx, goto ERR, "Syntax error: valid token expected");

	for(;;)
	{
		if(ERROR_CODE(int) == proto_ref_typeref_append(ret, next_tok->data->id))
		    _LIB_PROTO_ERROR(ctx, goto ERR);

		_TRY_CONSUME(ctx, goto ERR, 1);

		if(NULL == (next_tok = _peek(ctx, 1)))
		    _RAISE(ctx, goto ERR, "error: cannot peek next token");

		if(next_tok->type != LEXER_TOKEN_DOT)
		    break;

		_TRY_CONSUME(ctx, goto ERR, 1);

		if(NULL == (next_tok = _peek(ctx, 1)))
		    _RAISE(ctx, goto ERR, "error: canont peek next token");
	}

	return ret;
ERR:
	if(ret != NULL)
	    proto_ref_typeref_free(ret);
	return NULL;
}

/**
 * @brief parse the subscript of a field
 * @param ctx the context
 * @param buf the buffer for the dimension array
 * @param bufsize the buffer size
 * @return 0 if there's no subscript data, 1 if the subscript is successfully parsed, error code on error case
 **/
static inline int _parse_subscript(_context_t* ctx, uint32_t* buf, size_t bufsize)
{
	const lexer_token_t* tok = _peek(ctx, 1);
	if(NULL == tok)
	    _RAISE(ctx, return ERROR_CODE(int), "Internal error: cannot peek token");

	if(tok->type != LEXER_TOKEN_LBRACKET) return 0;

	_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);

	uint32_t len = 0;

	for(;;)
	{
		if(NULL == (tok = _peek(ctx, 1)) || tok->type != LEXER_TOKEN_NUMBER)
		    _RAISE(ctx, return ERROR_CODE(int), "syntax error: number token expected in the subscript");

		if(bufsize > len + 1)
		    buf[len++] = (uint32_t)tok->data->number;

		_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);

		if(NULL == (tok = _peek(ctx, 1)) || tok->type != LEXER_TOKEN_RBRACKET)
		    _RAISE(ctx, return ERROR_CODE(int), "syntax error: right-bracket expected");

		_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);

		if(NULL == (tok = _peek(ctx, 1)))
		    _RAISE(ctx, return ERROR_CODE(int), "error: cannot peek token");

		if(tok->type != LEXER_TOKEN_LBRACKET) break;

		_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);
	}

	buf[len] = 0;

	return 1;
}

static inline int _parse_primitive_field(_context_t* ctx, proto_type_t* type)
{
	const lexer_token_t* tok = _peek(ctx, 1);
	if(NULL == tok)
	    _RAISE(ctx, return ERROR_CODE(int), "Internal error: cannot peek token");

	uint32_t elem_size = tok->data->size;
	proto_type_atomic_metadata_t metadata = tok->metadata;

	_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);

	if(NULL == (tok = _peek(ctx, 1)) || tok->type != LEXER_TOKEN_ID)
	    _RAISE(ctx, return ERROR_CODE(int), "snytax error: identifer expected");

	static char namebuf[128];
	snprintf(namebuf, sizeof(namebuf), "%s", tok->data->id);

	_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);

	if(NULL == (tok = _peek(ctx, 1)))
	    _RAISE(ctx, return ERROR_CODE(int), "internal error: cannot peek token");

	static uint32_t dimensions[128];
	int rc = 0;
	int64_t ival = 0;
	double  dval = 0;
	float   fval = 0;

	if(tok->type == LEXER_TOKEN_EQUAL)
	{
		if(metadata.flags.numeric.invalid)
		    _RAISE(ctx, return ERROR_CODE(int), "syntax error: interger expected");
		/* This is a constant value */
		_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);
		if(NULL == (tok = _peek(ctx, 1)))
		    _RAISE(ctx, return ERROR_CODE(int), "internal error: cannot peek token");

		if(tok->type == LEXER_TOKEN_NUMBER)
		    ival = tok->data->number;
		else if(tok->type == LEXER_TOKEN_FLOAT_POINT)
		    dval = tok->data->floatpoint;
		else _RAISE(ctx, return ERROR_CODE(int), "syntax error: number expected");

		if(metadata.flags.numeric.is_real)
		{
			if(tok->type == LEXER_TOKEN_NUMBER) dval = (double)ival;
			if(elem_size == 4)
			{
				fval = (float)dval;
				metadata.numeric_default = &fval;
				metadata.flags.numeric.default_size = 4;
			}
			else
			{
				metadata.numeric_default = &dval;
				metadata.flags.numeric.default_size = 8;
			}
			elem_size = 0;
		}
		else if(tok->type == LEXER_TOKEN_NUMBER)
		{
			ival &= ((1ll << (8 * elem_size)) - 1);
			metadata.numeric_default = &ival;
			metadata.flags.numeric.default_size = elem_size & 0x1fffffffu;
			elem_size = 0;
		}
		else _RAISE(ctx, return ERROR_CODE(int), "syntax error: integer expected");
		_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);
	}
	else if(tok->type == LEXER_TOKEN_AT)
	{
		if(!metadata.flags.scope.valid)
		    _RAISE(ctx, return ERROR_CODE(int), "syntax error: scope object type expected");

		/* This is a @ cluase for the scope token */
		_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);
		if(NULL != (tok = _peek(ctx, 1)) && tok->type == LEXER_TOKEN_ID)
		{
			metadata.flags.scope.typename_size = strlen(tok->data->id) & 0x3fffffff;
			metadata.scope_typename = (char*)tok->data->id;
			_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);
		}
		else _RAISE(ctx, return ERROR_CODE(int), "syntax error: identifer exepcted");
	}
	else rc = _parse_subscript(ctx, dimensions, sizeof(dimensions) / sizeof(uint32_t));

	if(rc == ERROR_CODE(int))
	    return ERROR_CODE(int);

	if(ERROR_CODE(int) == proto_type_append_atomic(type, namebuf, elem_size, ((rc == 0) ? NULL : dimensions), &metadata))
	    _LIB_PROTO_ERROR(ctx, return ERROR_CODE(int));

	return 0;
}

static inline int _parse_user_type_field(_context_t* ctx, proto_type_t* type)
{
	const lexer_token_t* tok = NULL;
	proto_ref_typeref_t* field_type = _parse_type_ref(ctx);

	if(NULL == field_type)
	    return ERROR_CODE(int);

	if(NULL == (tok = _peek(ctx, 1)) || tok->type != LEXER_TOKEN_ID)
	    _RAISE(ctx, goto ERR, "snytax error: identifer expected");

	static char namebuf[128];
	snprintf(namebuf, sizeof(namebuf), "%s", tok->data->id);

	_TRY_CONSUME(ctx, goto ERR, 1);

	static uint32_t dimensions[128];
	int rc = _parse_subscript(ctx, dimensions, sizeof(dimensions) / sizeof(uint32_t));

	if(rc == ERROR_CODE(int))
	    return ERROR_CODE(int);

	if(ERROR_CODE(int) == proto_type_append_compound(type, namebuf, rc == 0 ? NULL : dimensions, field_type))
	    _LIB_PROTO_ERROR(ctx, goto ERR);

	return 0;
ERR:
	if(field_type != NULL) proto_ref_typeref_free(field_type);
	return ERROR_CODE(int);
}

static inline int _parse_alias_field(_context_t* ctx, proto_type_t* type)
{
	_TRY_CONSUME(ctx, return ERROR_CODE(int), 1);

	const lexer_token_t* tok = _peek(ctx, 1);
	if(NULL == tok || tok->type != LEXER_TOKEN_ID)
	    _RAISE(ctx, return ERROR_CODE(int), "syntax error: identifer expected");

	proto_ref_nameref_t* ref = proto_ref_nameref_new(32); /* TODO config */
	if(NULL == ref)
	    _LIB_PROTO_ERROR(ctx, return ERROR_CODE(int));

	int bracket_allowed = 0;

	for(;;)
	{
		switch(tok->type)
		{
			case LEXER_TOKEN_ID:
			    if(proto_ref_nameref_append_symbol(ref, tok->data->id) == ERROR_CODE(int))
			        _LIB_PROTO_ERROR(ctx, goto ERR);
			    _TRY_CONSUME(ctx, goto ERR, 1);
			    bracket_allowed = 1;
			    break;
			case LEXER_TOKEN_LBRACKET:
			    if(bracket_allowed)
			    {
				    _TRY_CONSUME(ctx, goto ERR, 1);
				    if(NULL == (tok = _peek(ctx, 1)) || tok->type != LEXER_TOKEN_NUMBER)
				        _RAISE(ctx, goto ERR, "syntax error: number expected");
				    if(proto_ref_nameref_append_subscript(ref, (uint32_t)tok->data->number) == ERROR_CODE(int))
				        _LIB_PROTO_ERROR(ctx, goto ERR);
				    _TRY_CONSUME(ctx, goto ERR, 1);
				    if(NULL == (tok = _peek(ctx, 1)) || tok->type != LEXER_TOKEN_RBRACKET)
				        _RAISE(ctx, goto ERR, "syntax error: right-bracket expected");
				    _TRY_CONSUME(ctx, goto ERR, 1);
				    break;
			    }
			default:
			    _RAISE(ctx, goto ERR, "syntax error: unexpected token");
		}
		if(NULL == (tok = _peek(ctx, 1)))
		    goto ERR;

		if(tok->type == LEXER_TOKEN_DOT)
		{
			_TRY_CONSUME(ctx, goto ERR, 1);
			if(NULL == (tok = _peek(ctx, 1)))
			    goto ERR;

			bracket_allowed = 0;
		}
		else if(!bracket_allowed || tok->type != LEXER_TOKEN_LBRACKET)
		    break;
	}

	if(tok->type != LEXER_TOKEN_ID)
	    _RAISE(ctx, goto ERR, "syntax error: identifer expected");

	if(ERROR_CODE(int) == proto_type_append_alias(type, tok->data->id, ref))
	    _LIB_PROTO_ERROR(ctx, goto ERR);

	ref = NULL;

	_TRY_CONSUME(ctx, goto ERR, 1);

	return 0;
ERR:
	if(NULL != ref)
	    proto_ref_nameref_free(ref);

	return ERROR_CODE(int);
}

/**
 * @brief parse a type definition
 * @param ctx the context
 * @note this will append the newly parsed type to the result array <br/>
 *       Because at this time we may not know the package name, so we will setup it later
 *       and know we just leave it blank.
 * @return status code
 **/
static inline int _parse_type(_context_t* ctx)
{
	compiler_type_t* type = (compiler_type_t*)malloc(sizeof(compiler_type_t));
	const lexer_token_t* tok = _peek(ctx, 1);
	proto_ref_typeref_t* basetype = NULL;
	if(NULL == tok)
	    _RAISE(ctx, return ERROR_CODE(int), "Internal error: cannot peek token");

	type->source = tok->file;
	type->package = NULL;
	type->next = NULL;

	_TRY_CONSUME(ctx, goto ERR, 1);

	if(NULL == (tok = _peek(ctx, 1)) || tok->type != LEXER_TOKEN_ID)
	    _RAISE(ctx, goto ERR, "syntax error: identifer expected");

	snprintf(type->name, sizeof(type->name), "%s", tok->data->id);

	_TRY_CONSUME(ctx, goto ERR, 1);

	if(NULL == (tok = _peek(ctx, 1)))
	    _RAISE(ctx, goto ERR, "error: can not peek next token");

	if(tok->type == LEXER_TOKEN_COLON)
	{
		/* Inheritance */
		_TRY_CONSUME(ctx, goto ERR, 1);
		if(NULL == (basetype = _parse_type_ref(ctx)))
		    _RAISE(ctx, goto ERR, "error: can not parse the basetype");
		if(NULL == (tok = _peek(ctx, 1)))
		    _RAISE(ctx, goto ERR, "error: can not peek next token");
	}

	if(tok->type != LEXER_TOKEN_LBRACE)
	    _RAISE(ctx, goto ERR, "syntax error: left-brace expected");

	_TRY_CONSUME(ctx, goto ERR, 1);

	if(NULL == (type->proto_type = proto_type_new(32 /* TODO: config this */, basetype, ctx->options.padding_size)))
	        _LIB_PROTO_ERROR(ctx, goto ERR);

	basetype = NULL;

	for(;;)
	{
		if(NULL == (tok = _peek(ctx, 1)))
		    _RAISE(ctx, goto ERR, "error: cannot peek the next token");

		switch(tok->type)
		{
			case LEXER_TOKEN_TYPE_PRIMITIVE:
			    if(_parse_primitive_field(ctx, type->proto_type) == ERROR_CODE(int))
			        _RAISE(ctx, goto ERR, "error: cannot parse the prmitive type");
			    break;
			case LEXER_TOKEN_ID:
			    if(_parse_user_type_field(ctx, type->proto_type) == ERROR_CODE(int))
			        _RAISE(ctx, goto ERR, "error: cannot parse the user-defined typed field");
			    break;
			case LEXER_TOKEN_K_ALIAS:
			    if(_parse_alias_field(ctx, type->proto_type) == ERROR_CODE(int))
			        _RAISE(ctx, goto ERR, "error: cannot parse the alias field");
			    break;
			case LEXER_TOKEN_RBRACE:
			    _TRY_CONSUME(ctx, goto ERR, 1);
			    goto TYPE_END;
			case LEXER_TOKEN_SEMICOLON:
			    _TRY_CONSUME(ctx, goto ERR, 1);
			    break;
			default:
			    _RAISE(ctx, goto ERR, "syntax error: unexpected token");
		}
	}


TYPE_END:

	type->next = ctx->result->type_list;
	ctx->result->type_list = type;

	return 0;
ERR:
	if(type != NULL)
	{
		if(type->proto_type != NULL)
		    proto_type_free(type->proto_type);
		free(type);
	}
	if(NULL != basetype)
	    proto_ref_typeref_free(basetype);
	return 0;
}

compiler_result_t* compiler_compile(compiler_options_t options)
{
	uint32_t i;
	if(options.lexer == NULL)
	    ERROR_PTR_RETURN_LOG("Invaild arguments");

	_context_t ctx = {
		.options = options,
		.result = (compiler_result_t*)calloc(1, sizeof(compiler_result_t)),
		.lexer  = options.lexer,
		.lh_buffer = {NULL, NULL, NULL},
		.lh_begin  = 0
	};


	if(ctx.result == NULL)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the result");

	for(;;)
	{
		const lexer_token_t* token;
		if(NULL == (token = _peek(&ctx, 1)))
		    ERROR_LOG_GOTO(ERR, "Cannot peek the lookahead buffer");
		switch(token->type)
		{
			case LEXER_TOKEN_EOF:
			    goto DONE;
			case LEXER_TOKEN_K_PACKAGE:
			    _TRY_CONSUME(&ctx, goto ERR, 1);
			    if(ctx.result->package != NULL)
			        _RAISE(&ctx, goto ERR,  "syntax error: duplicated package statement");
			    if(NULL == (ctx.result->package = _parse_type_ref(&ctx)))
			        goto ERR;
			    break;

			case LEXER_TOKEN_K_TYPE:
			    if(ERROR_CODE(int) == _parse_type(&ctx))
			        ERROR_LOG_GOTO(ERR, "Cannot parse the type definition block");
			    break;

			case LEXER_TOKEN_SEMICOLON:
			    _consume(&ctx, 1);
			    break;
			default:
			    _RAISE(&ctx, goto ERR, "Unexpected token");
		}
	}

DONE:
	for(i = 0; i < sizeof(ctx.lh_buffer) / sizeof(ctx.lh_buffer[0]); i ++)
	    if(NULL != ctx.lh_buffer[i])
	        lexer_token_free(ctx.lh_buffer[i]);

	compiler_type_t* curtype;
	for(curtype = ctx.result->type_list; curtype != NULL; curtype = curtype->next)
	    if(NULL == (curtype->package = ctx.result->package == NULL ? NULL : proto_ref_typeref_get_path(ctx.result->package)))
	        goto ERR;
	return ctx.result;
ERR:

	for(i = 0; i < sizeof(ctx.lh_buffer) / sizeof(ctx.lh_buffer[0]); i ++)
	    if(NULL != ctx.lh_buffer[i])
	        lexer_token_free(ctx.lh_buffer[i]);
	if(ctx.result != NULL) compiler_result_free(ctx.result);
	return NULL;
}

int compiler_result_free(compiler_result_t* result)
{
	int rc = 0;

	if(NULL == result)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	compiler_type_t* ptr;

	for(ptr = result->type_list; NULL != ptr;)
	{
		compiler_type_t* current = ptr;
		ptr = ptr->next;
		proto_err_clear();
		if(NULL != current->proto_type && ERROR_CODE(int) == proto_type_free(current->proto_type))
		{
			rc = ERROR_CODE(int);
		}

		free(current);
	}

	if(NULL != result->filename) free(result->filename);
	if(NULL != result->package)  proto_ref_typeref_free(result->package);
	free(result);

	return rc;
}
