/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <constants.h>
#include <error.h>

#include <utils/vector.h>
#include <utils/log.h>
#include <utils/string.h>

#include <runtime/api.h>
#include <runtime/pdt.h>

/**
 * @brief the implementation of the PDT
 **/
struct _runtime_pdt_t {
	runtime_api_pipe_id_t input_count;   /* how many inputs */
	runtime_api_pipe_id_t output_count;  /* how many outputs */
	vector_t* vec;
};

/**
 * @brief a name entry in a pipe description table
 **/
typedef struct {
	char name[RUNTIME_PIPE_NAME_LEN];   /*!< the name of the pipe */
	runtime_api_pipe_flags_t flags;     /*!< the flag bits of the pipe */
	char* type_expr;                    /*!< the type expression of this entry */
	runtime_api_pipe_type_callback_t type_callback;   /*!< The callback function called after the type of the pipe has been inferred */
	void* type_callback_data;                         /*!< The additional data will be sent to the type inference callback */
} _name_entry_t;

/**
 * @brief search the PDT table by name
 * @param table the target PDT table
 * @param name the name of the pipe to search
 * @return the pipe id has been found, or error code when no such entry
 **/
static inline runtime_api_pipe_id_t _search_table(const runtime_pdt_t* table, const char* name)
{
	size_t i;
	for(i = 0; i < vector_length(table->vec); i ++)
	{
		const _name_entry_t* entry = VECTOR_GET_CONST(_name_entry_t, table->vec, i);
		if(strcmp(entry->name, name) == 0)
		{
			LOG_DEBUG("Found name table entry (name = %s, id = %zu)", entry->name, i);
			return (runtime_api_pipe_id_t)i;
		}
	}
	return ERROR_CODE(runtime_api_pipe_id_t);
}

runtime_pdt_t* runtime_pdt_new()
{
	runtime_pdt_t* ret = (runtime_pdt_t*)malloc(sizeof(runtime_pdt_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Could not allocate memory for PDT");

	ret->vec = vector_new(sizeof(_name_entry_t), RUNTIME_PDT_INIT_SIZE);

	ret->input_count = ret->output_count = 0;

	if(NULL == ret->vec)
	{
		free(ret);
		ERROR_PTR_RETURN_LOG("Could not create vector for PDT");
	}

	return ret;
}

int runtime_pdt_free(runtime_pdt_t* table)
{
	if(NULL == table) return ERROR_CODE(int);
	if(NULL == table->vec)
	{
		free(table);
		return ERROR_CODE(int);
	}

	uint32_t i;
	for(i = 0; i < vector_length(table->vec); i ++)
	{
		_name_entry_t* entry = VECTOR_GET(_name_entry_t, table->vec, i);
		if(NULL != entry && NULL != entry->type_expr)
		    free(entry->type_expr);
	}

	int rc = vector_free(table->vec);
	free(table);
	return rc;
}

runtime_api_pipe_id_t runtime_pdt_insert(runtime_pdt_t* table, const char* name, runtime_api_pipe_flags_t flags, const char* type_expr)
{
	if(NULL == table || NULL == name)
	    ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Invalid arguments");

	if(_search_table(table, name) != ERROR_CODE(runtime_api_pipe_id_t))
	    ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Duplicated pipe name in the same table");

	char* type_buf = NULL;
	if(type_expr != NULL)
	{
		size_t len = strlen(type_expr);
		if(NULL == (type_buf = (char*)malloc(len + 1)))
		    ERROR_RETURN_LOG_ERRNO(runtime_api_pipe_id_t, "Cannot allocate buffer for the type expression");
		memcpy(type_buf, type_expr, len + 1);
	}

	vector_t* new_vec = vector_append(table->vec, NULL);
	if(NULL == new_vec) ERROR_LOG_GOTO(ERR, "can not create a new entry in PDT");

	table->vec = new_vec;
	size_t id = vector_length(new_vec) - 1;
	_name_entry_t* last = VECTOR_GET(_name_entry_t, new_vec, id);

	if(NULL == last)
	    ERROR_LOG_GOTO(ERR, "Can not get the last element in PDT");

	string_buffer_t namebuf;
	string_buffer_open(last->name, RUNTIME_PIPE_NAME_LEN, &namebuf);
	string_buffer_append(name, &namebuf);
	string_buffer_close(&namebuf);

	last->flags = flags;
	last->type_expr = type_buf;
	last->type_callback = NULL;
	last->type_callback_data = NULL;

	LOG_DEBUG("New PDT entry (pd=%zu, name=%s, flags=%x)", id, name, flags);

	if(RUNTIME_API_PIPE_IS_INPUT(flags))
	    table->input_count ++;
	else if(RUNTIME_API_PIPE_IS_OUTPUT(flags))
	    table->output_count ++;
	else LOG_WARNING("Invalid pipe definition flags 0x%x", flags);

	return (runtime_api_pipe_id_t)id;

ERR:
	if(NULL != type_buf)
	    free(type_buf);

	return ERROR_CODE(runtime_api_pipe_id_t);
}

runtime_api_pipe_id_t runtime_pdt_get_pd_by_name(const runtime_pdt_t* table, const char* name)
{
	if(NULL == table || NULL == name) ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Invalid arguments");

	return _search_table(table, name);
}

runtime_api_pipe_flags_t runtime_pdt_get_flags_by_pd(const runtime_pdt_t* table, runtime_api_pipe_id_t pd)
{
	if(NULL == table || pd == ERROR_CODE(runtime_api_pipe_id_t) || pd >= vector_length(table->vec))
	    ERROR_RETURN_LOG(runtime_api_pipe_flags_t, "Invalid arguments");

	return VECTOR_GET_CONST(_name_entry_t, table->vec, pd)->flags;
}

const char* runtime_pdt_get_name(const runtime_pdt_t* table, runtime_api_pipe_id_t pd)
{
	if(NULL == table || pd == ERROR_CODE(runtime_api_pipe_id_t) || pd >= vector_length(table->vec))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	return VECTOR_GET_CONST(_name_entry_t, table->vec, pd)->name;
}

runtime_api_pipe_id_t runtime_pdt_get_size(const runtime_pdt_t* table)
{
	if(NULL == table) ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Invalid arguments");

	return (runtime_api_pipe_id_t)vector_length(table->vec);
}

runtime_api_pipe_id_t runtime_pdt_input_count(const runtime_pdt_t* table)
{
	if(NULL == table) ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Invalid arguments");

	return table->input_count;
}

runtime_api_pipe_id_t runtime_pdt_output_count(const runtime_pdt_t* table)
{
	if(NULL == table) ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Invalid arguments");

	return table->output_count;
}

const char* runtime_pdt_type_expr(const runtime_pdt_t* table, runtime_api_pipe_id_t pid)
{
	if(NULL == table || ERROR_CODE(runtime_api_pipe_id_t) == pid || pid >= vector_length(table->vec))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const char* ret = VECTOR_GET_CONST(_name_entry_t, table->vec, pid)->type_expr;
	if(NULL == ret) ret = UNTYPED_PIPE_HEADER;

	return ret;
}

int runtime_pdt_set_type_hook(runtime_pdt_t* table, runtime_api_pipe_id_t pid, runtime_api_pipe_type_callback_t callback, void* data)
{
	if(NULL == table || ERROR_CODE(runtime_api_pipe_id_t) == pid || pid >= vector_length(table->vec))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	_name_entry_t* entry = VECTOR_GET(_name_entry_t, table->vec, pid);

	if(NULL == entry)
	    ERROR_RETURN_LOG(int, "Cannot access the PDT");

	entry->type_callback = callback;
	entry->type_callback_data = data;

	return 0;
}

int runtime_pdt_get_type_hook(const runtime_pdt_t* table, runtime_api_pipe_id_t pid, runtime_api_pipe_type_callback_t* result_func, void** result_data)
{
	if(NULL == table || ERROR_CODE(runtime_api_pipe_id_t) == pid || pid >= vector_length(table->vec) || NULL == result_func)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _name_entry_t*  entry = VECTOR_GET_CONST(_name_entry_t, table->vec, pid);

	if(NULL == entry)
	    ERROR_RETURN_LOG(int, "Cannot access the PDT");

	*result_func = entry->type_callback;
	*result_data = entry->type_callback_data;

	return 0;
}
