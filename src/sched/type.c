/**
 * Copyright (C) 2017, Hao Hou
 **/
#define _GNU_SOURCE

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <error.h>

#include <utils/log.h>
#include <utils/hashmap.h>
#include <utils/vector.h>
#include <utils/string.h>

#include <itc/module_types.h>
#include <itc/module.h>

#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <sched/service.h>
#include <sched/type.h>

#include <proto.h>

/**
 * @brief the evnrionment table
 **/
typedef struct {
	hashmap_t* namemap;   /*!< the hash map maps variable name to the variable id */
	vector_t*  values;    /*!< the value array, the data in the values should be actually the pointer to the array of parsed compound types */
} _env_t;

/**
 * @brief duplicate a parsed type
 * @param type the type to duplicate
 * @note in this function we assume that all the type name is managed by libproto
 * @return the duplicated result
 **/
static inline char const** _dup_type(char const* const* type)
{
	uint32_t count;
	for(count = 0; type[count] != NULL; count ++);

	char const* * ret = (char const**)malloc(sizeof(*ret) * (count + 1));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the parsed type");

	memcpy(ret, type, sizeof(*ret) * (count + 1));

	return ret;
}

/**
 * @brief parse a type expression to the array of simple type name
 * @param type_expr the type expression
 * @note  For the type name, we return a point that has been allocated inside the libproto memory space <br/>
 *        For the type variables, we return the pointer where the type name begins <br/>
 *        By doing this we can check if two type name are the same by comparing the type name
 * @return the parse result array, or NULL on error case
 **/
static inline char const** _parse_type(const char* type_expr)
{
	char type_buf[PATH_MAX];

	uint32_t nsect, start, i;

	for(start = 0, nsect = 0; type_expr[start];)
	{
		/* Strip the leading whitespaces  */
		int is_alt = 0;
		for(;type_expr[start] && (type_expr[start] == ' ' || type_expr[start] == '\t' || type_expr[start] == '|'); start ++)
		    if(type_expr[start] == '|') is_alt = 1;

		/* If it's the alternation operator, we need to add another NULL between two different options */
		if(is_alt) nsect ++;

		int has_content = 0;
		/* Go through the simple type or type variable */
		for(;type_expr[start] && type_expr[start] != ' ' && type_expr[start] != '\t' && type_expr[start] != '|'; start ++)
		    has_content = 1;

		if(has_content) nsect ++;
	}

	LOG_DEBUG("There are %u sections in the type expresssion: %s", nsect, type_expr);

	char const** ret = (char const**)malloc(sizeof(*ret) * (nsect + 2));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the parsed type expression");

	ret[nsect + 1] = NULL;

	for(i = 0; i < nsect; i ++)
	{
		uint32_t len;

		/* Strip the leading whitespaces */
		int is_alt = 0;
		for(len = 0;*type_expr != 0 && (*type_expr == ' ' || *type_expr == '\t' || *type_expr == '|'); type_expr ++, len ++)
		    if(*type_expr == '|') is_alt = 1;


		if(is_alt)
		{
			ret[i] = NULL;
			continue;
		}

		/* Then we need to copy all the content to the type buffer */
		for(len = 0;*type_expr != 0 && *type_expr != ' ' && *type_expr != '\t' && *type_expr != '|'; type_expr ++, len ++)
		    type_buf[len] = *type_expr;
		type_buf[len] = 0;

		if(type_buf[0] != '$')
		{
			/* Then we have a type name, basically we want to convert the type name to a pointer to the libproto managed type name buffer */
			if(NULL == (ret[i] = proto_db_get_managed_name(type_buf)))
			    ERROR_LOG_GOTO(ERR, "Libproto can not find the type named %s", type_buf);
		}
		else
		{
			/* Otherwise we have a type variable */
			ret[i] = type_expr - len;
		}
	}

	ret[nsect] = NULL;

	return ret;
ERR:

	if(NULL != ret) free(ret);

	return NULL;
}

/**
 * @brief create a new environment table
 * @return the newly created environment table, NULL on error cases
 **/
static inline _env_t* _env_new()
{
	_env_t* ret = (_env_t*)calloc(1, sizeof(*ret));
	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the environment table");

	/* TODO: get rid of magic numbers */
	if(NULL == (ret->namemap = hashmap_new(SCHED_TYPE_ENV_HASH_SIZE, 128)))
	    ERROR_LOG_ERRNO_GOTO(ERR,"Cannot create the name hash map for the environment table");

	if(NULL == (ret->values = vector_new(sizeof(char const* const*), 128)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create the value vector for the environment table");

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->namemap)
		    hashmap_free(ret->namemap);
		if(NULL != ret->values)
		    vector_free(ret->values);
	}

	return NULL;
}

/**
 * @brief dispose a used environment table
 * @param env the environment table to dispose
 * @return status code
 **/
static inline int _env_free(_env_t* env)
{
	int rc = 0;
	if(NULL != env)
	{
		if(NULL != env->namemap && ERROR_CODE(int) == hashmap_free(env->namemap))
		    rc = ERROR_CODE(int);

		if(NULL != env->values)
		{
			size_t size = vector_length(env->values);
			size_t i;
			for(i = 0; i < size; i ++)
			{
				char const* * slot = *VECTOR_GET_CONST(char const* *, env->values, i);
				if(NULL != slot) free(slot);
			}

			if(ERROR_CODE(int) == vector_free(env->values))
			    rc = ERROR_CODE(int);
		}

		free(env);
	}

	return rc;
}

/**
 * @brief get the type name from the environment table
 * @param env the environment table
 * @param varname the variable name
 * @param resbuf the result buffer
 * @return status code
 * @note be aware of the difference between the error state and the state we can't find
 *       a variable.
 *       Error case means we encounter some error such as the hashmap failure, in this
 *       case the status code should be the error code.
 *       If the variable not found, the resbuf will be set to NULL and returns a success
 *       status code
 **/
static inline int _env_get(const _env_t* env, const char* varname, char const* * * resbuf)
{
	int rc;
	hashmap_find_res_t result;
	if(ERROR_CODE(int) == (rc = hashmap_find(env->namemap, varname, strlen(varname), &result)))
	    ERROR_RETURN_LOG(int, "Cannot look up the variable from the environment table");

	*resbuf = NULL;

	if(rc == 0) return 0;

	uint32_t idx = *(uint32_t*)result.val_data;

	if(idx >= vector_length(env->values))
	    ERROR_RETURN_LOG(int, "Invalid variable ID %u", idx);

	*resbuf = *VECTOR_GET_CONST(char const* *, env->values, idx);

	return 0;
}

/**
 * @brief merge a type variable assignment to the environment
 * @details This function is very similar to the assignment operation in the normal scope.
 *           But the only difference is when the environment already have the variable named
 *           the requested name. Instead of overriding the variable, it actually compute the
 *           common ancestor of current variable value and the concrete type to be assigned. <br/>
 *           When the merge result is NULL, we consider this merge as error, because we have
 *           the limit that, we can never generalize any typed pipe to a untyped pipe <br/>
 * @param env the environment table
 * @param varname the variable name
 * @param concrete_type the type to be assigned to variable
 * @return status code
 **/
static inline int _env_merge(_env_t* env, const char* varname, char const* const* concrete_type)
{
	char const* * current;

	if(ERROR_CODE(int) == _env_get(env, varname, &current))
	    ERROR_RETURN_LOG(int, "Cannot look up the environment table");

	if(NULL == current)
	{
		/* If this variable name isn't previously defined, we just do a simple assignment */
		size_t len = strlen(varname);
		size_t next_id = vector_length(env->values);
		if(ERROR_CODE(size_t) == next_id)
		    ERROR_RETURN_LOG(int, "Cannot get the next index");
		if(ERROR_CODE(int) == hashmap_insert(env->namemap, varname, len, &next_id, sizeof(next_id), NULL, 0))
		    ERROR_RETURN_LOG(int, "Cannot insert the name map to the name map");

		const char** copied = _dup_type(concrete_type);
		if(NULL == copied)
		    ERROR_RETURN_LOG(int, "Cannot duplicate the type");

		vector_t* new = vector_append(env->values, &copied);
		if(NULL == new)
		{
			LOG_ERROR("Cannot insert the new item in the environment");
			free(copied);
		}

		env->values = new;
		return 0;
	}
	else
	{
		uint32_t i;
		for(i = 0; concrete_type[i] != NULL && current[i] != NULL; i ++);
		if(concrete_type[i] != NULL || current[i] != NULL)
		    ERROR_RETURN_LOG(int, "Cannot merge type due to different length");

		for(i = 0; concrete_type[i] != NULL && current[i] != NULL; i ++)
		{
			if(concrete_type[i][0] == '$')
			    ERROR_RETURN_LOG(int, "Invalid arguments: cannot assign a type pattern to a type variable");

			const char* type_to_merge[] = {
				concrete_type[i],
				current[i],
				NULL
			};

			const char* merged_type = proto_db_common_ancestor(type_to_merge);

			if(NULL == merged_type)
			    ERROR_RETURN_LOG(int, "Cannot merge type %s and %s", concrete_type[i], current[i]);

			current[i] = merged_type;
		}

		return 0;
	}
}

/**
 * @brief solve the convertibility equation system
 * @param service the service
 * @param env the environment table
 * @param incomings the incoming pipe table
 * @param incoming_count the size of incoming pipe table
 * @return status code
 **/
static inline int _solve_ces(const sched_service_t* service, _env_t* env, const sched_service_pipe_descriptor_t* incomings, uint32_t incoming_count)
{
	uint32_t i;
	for(i = 0; i < incoming_count; i ++)
	{
		const sched_service_pipe_descriptor_t* pd = incomings + i;
		const char* sour_type;
		if(ERROR_CODE(int) == sched_service_get_pipe_type(service, pd->source_node_id, pd->source_pipe_desc, &sour_type))
		    ERROR_LOG_GOTO(ERR, "Cannot get the actual type for the source pipe endpoint");
		if(NULL == sour_type)
		    ERROR_LOG_GOTO(ERR, "We don't know the type of the source endpoint, may be a code bug");

		const char* dest_expr = sched_service_get_pipe_type_expr(service, pd->destination_node_id, pd->destination_pipe_desc);
		if(NULL == dest_expr)
		    ERROR_LOG_GOTO(ERR, "Cannot get the type expression for the destination pipe end point");

		LOG_DEBUG("The convertibility equation from pipe <NID=%u, PID=%u> -> <NID=%u, PID=%u>: %s => %s",
		          pd->source_node_id, pd->source_pipe_desc,
		          pd->destination_node_id, pd->destination_pipe_desc,
		          sour_type, dest_expr);

		char const* * parsed_sour_type = NULL;
		char const* * parsed_dest_type = NULL;
		parsed_sour_type = _parse_type(sour_type);
		if(NULL == parsed_sour_type)
		    ERROR_LOG_GOTO(LOOP_ERR, "Cannot parse the source type for the convertibility equation");

		parsed_dest_type = _parse_type(dest_expr);
		if(NULL == parsed_dest_type)
		    ERROR_LOG_GOTO(LOOP_ERR, "Cannot parse the destination type for the convertibility equation");

		uint32_t i;
		for(i = 0; parsed_sour_type[i] != NULL && parsed_dest_type[i] != NULL; i++)
		{
			const char* from_type = parsed_sour_type[i];
			const char* to_type   = parsed_dest_type[i];

			if(to_type[0] == '$')
			{
				/* Because the size limit of a type expression is PATH_MAX, so we can not have a variable longer than PATH_MAX */
				char varname[PATH_MAX];
				uint32_t j;
				for(j = 1; to_type[j] != 0 && to_type[j] != ' ' && to_type[j] != '\t' && to_type[j] != '|'; j ++)
				    varname[j - 1] = to_type[j];

				varname[j - 1] = 0;

				if(j == 1)
				    ERROR_LOG_GOTO(LOOP_ERR, "Invalid type variable name $");

				if(parsed_dest_type[i + 1] == NULL)
				{
					LOG_DEBUG("The trailing type variable %s, capturing everything on the left side", varname);

					if(ERROR_CODE(int) == _env_merge(env, varname, parsed_sour_type + i))
					    ERROR_LOG_GOTO(LOOP_ERR, "Cannot merge the type expression to the variable");
				}
				else
				{
					LOG_DEBUG("This isn't a trialing type variable (named %s), so we only try to map a simple type to it", varname);

					const char* simple_type[] = {parsed_dest_type[i], NULL};

					if(ERROR_CODE(int) == _env_merge(env, varname, simple_type))
					    ERROR_LOG_GOTO(LOOP_ERR, "Cannot merge the type expression to the variable");
				}
			}
			else
			{
				const char* types[] = {
					from_type,
					to_type,
					NULL
				};
				const char* common_ancestor = proto_db_common_ancestor(types);
				if(common_ancestor == NULL || strcmp(common_ancestor, to_type) != 0)
				    ERROR_LOG_GOTO(LOOP_ERR, "Invalid conversion: %s -> %s", from_type, to_type);
			}
		}

		free(parsed_dest_type);
		free(parsed_sour_type);
		continue;
LOOP_ERR:
		if(NULL != parsed_sour_type) free(parsed_sour_type);
		if(NULL != parsed_dest_type) free(parsed_dest_type);
		goto ERR;
	}

	return 0;
ERR:
	return ERROR_CODE(int);
}

/**
 * @brief render the concrete type name from the type expression under the given environment
 * @param type_expr the type expression we want to render
 * @param env the environment table
 * @param buf the result buffer
 * @param size the size of result buffer
 * @param header_size_buf the buffer used to return the header size
 * @return the render result, NULL on error case
 **/
static inline const char* _render_type_name(const char* type_expr, const _env_t* env, char* buf, size_t size, size_t* header_size_buf)
{
	char const* * parsed_type = _parse_type(type_expr);
	if(NULL == parsed_type)
	    ERROR_PTR_RETURN_LOG("Cannot parse the type expression: %s", type_expr);

	uint32_t      merged_type_cap = 8;
	uint32_t      merged_type_len = 0;
	uint32_t      first_alt = 1;
	char const* * merged_type = (char const* *)malloc(sizeof(merged_type[0]) * merged_type_cap);

	if(NULL == merged_type)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the merged type buffer");

	/* This is safe, because the inner loop will exit when it see the first NULL, but if there's
	 * a first NULL there should be either another NULL or a meaningful block following */

	uint32_t i;
	for(i = 0; parsed_type[i] != NULL;)
	{
		uint32_t sec = 0;
		for(; parsed_type[i] != NULL; i ++)
		{
			char const* * result;
			const char* simple_result[2] = {NULL, NULL};
			if(parsed_type[i][0] == '$')
			{
				/* This is a type variable */
				uint32_t len = 0, flen = 0;
				char varname[PATH_MAX];
				char fieldname[PATH_MAX];
				const char* start = parsed_type[i] + 1;
				for(;*start && *start != ' ' && *start != '\t' && *start != '|' && *start != '.'; start ++, len ++)
				    varname[len] = *start;
				varname[len] = 0;

				if(ERROR_CODE(int) == _env_get(env, varname, &result))
				    ERROR_LOG_GOTO(ERR, "Cannot look for %s in the environment table", varname);

				if(*start == '.')
				{
					for(start ++; *start && *start != '\t' && *start != ' ' && *start != '|'; start ++, flen ++)
					    fieldname[flen] = *start;
					fieldname[flen] = 0;

					const char* underlying = NULL;
					if(NULL == (underlying = proto_db_field_type(result[0], fieldname)))
					    ERROR_LOG_GOTO(ERR, "Cannot get the type of field [%s = %s].%s", varname, result[0], underlying);

					LOG_DEBUG("Expand field type expression [%s = %s].%s = %s", varname, result[0], fieldname, underlying);

					simple_result[0] = underlying;
					result = simple_result;
				}


				if(NULL == result)
				    ERROR_LOG_GOTO(ERR, "Variable %s not found", varname);

			}
			else
			{
				/* This is a type constant */
				simple_result[0] = parsed_type[i];
				result = simple_result;
			}

			uint32_t j;
			for(j = 0; result[j] != NULL; j ++, sec ++)
			{
				if(first_alt)
				{
					if(merged_type_cap <= merged_type_len)
					{
						uint32_t next_cap = merged_type_cap * 2;
						char const* * new_buf = (char const* *)realloc(merged_type, next_cap * sizeof(merged_type[0]));
						if(NULL == new_buf)
						    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resize the merged type buffer");
						merged_type_cap = next_cap;
						merged_type = new_buf;
					}
					merged_type[merged_type_len ++] = result[j];
				}
				else
				{
					if(sec >= merged_type_len)
					    ERROR_LOG_GOTO(ERR, "The alternations are in different size");

					const char* merge_buf[] = {
						merged_type[sec],
						result[j],
						NULL
					};

					const char* merge_result = proto_db_common_ancestor(merge_buf);

					if(NULL == merge_result)
					    ERROR_LOG_GOTO(ERR, "Cannot merge type %s and %s", merged_type[sec], result[j]);

					merged_type[sec] = merge_result;
				}
			}
		}
		first_alt = 0;
		i++;
	}

	string_buffer_t sbuf;
	string_buffer_open(buf, size, &sbuf);

	*header_size_buf = 0;

	for(i = 0; i < merged_type_len; i ++)
	{
		if(i > 0)
		    string_buffer_append(" ", &sbuf);
		else
		{
			/* We need to calcuate the header size of the buffer */
			const char* typename = merged_type[i];
			*header_size_buf = proto_db_type_size(typename);
			LOG_DEBUG("The typename %s has a size of %zu bytes", typename, *header_size_buf);

		}
		string_buffer_append(merged_type[i], &sbuf);
	}

	free(parsed_type);
	free(merged_type);
	return string_buffer_close(&sbuf);
ERR:
	if(NULL != parsed_type) free(parsed_type);
	if(NULL != merged_type) free(merged_type);
	return NULL;
}

/**
 * @brief perform type checking and inference on the given node of the given service
 * @param service the target service
 * @param node the node we want to infer
 * @return status code
 **/
static inline int _infer_node(sched_service_t* service, sched_service_node_id_t node)
{
	uint32_t incoming_count, outgoing_count;
	const sched_service_pipe_descriptor_t* incomings = sched_service_get_incoming_pipes(service, node, &incoming_count);

	if(NULL == incomings)
	    ERROR_RETURN_LOG(int, "Cannot get the incoming pipes");

	const sched_service_pipe_descriptor_t* outgoings = sched_service_get_outgoing_pipes(service, node, &outgoing_count);

	if(NULL == outgoings)
	    ERROR_RETURN_LOG(int, "Cannot get the outgoing pipes");

	_env_t* env;
	if((env = _env_new()) == NULL)
	    ERROR_LOG_GOTO(ERR, "Cannot create the environment table for the servlet type inferrer");

	if(ERROR_CODE(int) == _solve_ces(service, env, incomings, incoming_count))
	    ERROR_LOG_GOTO(ERR, "Cannot solve the consertibility equation system");

	uint32_t i;
	char result[SCHED_TYPE_MAX];
	for(i = 0; i < incoming_count; i ++)
	{
		const sched_service_pipe_descriptor_t* pd = incomings + i;

		const char* type_expr = sched_service_get_pipe_type_expr(service, pd->destination_node_id, pd->destination_pipe_desc);

		if(NULL == type_expr)
		    ERROR_LOG_GOTO(ERR, "Cannot get the type expression for the destination pipe");

		size_t size;

		const char* actual_type = _render_type_name(type_expr, env, result, sizeof(result), &size);
		if(NULL == actual_type)
		    ERROR_LOG_GOTO(ERR, "Cannot render the type expression");

		LOG_DEBUG("Pipe <NID=%u, PID=%u> has type %s (%zu bytes)", pd->destination_node_id, pd->destination_pipe_desc, actual_type, size);

		if(ERROR_CODE(int) == sched_service_set_pipe_type(service, pd->destination_node_id, pd->destination_pipe_desc, actual_type, size))
		    ERROR_LOG_GOTO(ERR, "Cannot set the actual type for the pipe");
	}

	for(i = 0; i < outgoing_count; i ++)
	{
		const sched_service_pipe_descriptor_t* pd = outgoings + i;

		const char* type_expr = sched_service_get_pipe_type_expr(service, pd->source_node_id, pd->source_pipe_desc);
		if(NULL == type_expr)
		    ERROR_LOG_GOTO(ERR, "Cannot get the type expression for the source pipe endpoint");

		size_t size;
		const char* actual_type = _render_type_name(type_expr, env, result, sizeof(result), &size);
		if(NULL == actual_type)
		    ERROR_LOG_GOTO(ERR, "Cannot render the type expression");

		LOG_DEBUG("Pipe <NID=%u, PID=%u> has type %s (%zu bytes)", pd->source_node_id, pd->source_pipe_desc, actual_type, size);

		if(ERROR_CODE(int) == sched_service_set_pipe_type(service, pd->source_node_id, pd->source_pipe_desc, actual_type, size))
		    ERROR_LOG_GOTO(ERR, "Cannot set the actual type for the pipe");
	}

	if(ERROR_CODE(int) == (_env_free(env)))
	    ERROR_RETURN_LOG(int, "Cannot dispose the environment table");
	return 0;
ERR:
	if(NULL != env) _env_free(env);
	return ERROR_CODE(int);
}

int sched_type_check(sched_service_t* service)
{
	int rc = ERROR_CODE(int);
	uint32_t* degree = NULL;
	sched_service_node_id_t* stack = NULL;
	int sp = 1;

	if(NULL == service)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == proto_init())
	    ERROR_RETURN_LOG(int, "Cannot initialize libproto");

	size_t num_node = sched_service_get_num_node(service);
	if(ERROR_CODE(size_t) == num_node)
	    ERROR_LOG_GOTO(ERR, "Cannot get the number of nodes");

	if(NULL == (degree = (uint32_t*)malloc(sizeof(degree[0]) * num_node)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate the degree array");

	if(NULL == (stack = (sched_service_node_id_t*)malloc(sizeof(stack[0]) * num_node)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate the stack array");

	memset(degree, 0, sizeof(int) * num_node);
	sched_service_node_id_t i;
	sched_service_node_id_t input = sched_service_get_input_node(service);
	if(ERROR_CODE(sched_service_node_id_t) == input)
	    ERROR_LOG_GOTO(ERR, "Cannot get the input pipe");

	stack[0] = input;

	for(i = 0; i< num_node; i ++)
	    if(NULL == sched_service_get_incoming_pipes(service, i, degree + i))
	        ERROR_LOG_GOTO(ERR, "Cannot get the number of incoming pipes");

	while(sp > 0)
	{
		sched_service_node_id_t current = stack[--sp];

		if(ERROR_CODE(int) == _infer_node(service, current))
		    ERROR_LOG_GOTO(ERR, "Cannot infer the type of pipes for node %u", current);

		uint32_t npds;
		const sched_service_pipe_descriptor_t* pds = sched_service_get_outgoing_pipes(service, current, &npds);
		if(NULL == pds)
		    ERROR_LOG_GOTO(ERR, "Cannot get the output pipes");

		uint32_t i;
		for(i = 0; i < npds; i ++)
		{
			const sched_service_pipe_descriptor_t* pd = pds + i;
			if(--degree[pd->destination_node_id] == 0)
			    stack[sp ++] = pd->destination_node_id;
		}
	}

	rc = 0;
ERR:
	if(ERROR_CODE(int) == proto_finalize())
	{
		LOG_ERROR("Cannot dispose libproto");
		rc = ERROR_CODE(int);
	}

	if(NULL != degree) free(degree);
	if(NULL != stack) free(stack);
	return rc;
}
