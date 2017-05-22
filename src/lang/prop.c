/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <plumber.h>
#include <utils/log.h>
#include <utils/vector.h>
#include <utils/static_assertion.h>
#include <error.h>
/**
 * @brief represents a pair of callback functions
 **/
typedef struct _callback_t{
	const void* param;          /*!< the param for the getter or setter */
	lang_prop_get_func_t get;   /*!< the getter function */
	lang_prop_set_func_t set;   /*!< the setter function */
	uint32_t num_sections;      /*!< how many sections in the table */
	uintptr_t __pading__[0];
	uint32_t sym[0];            /*!< the symbol array */
} _callback_t;
STATIC_ASSERTION_LAST(_callback_t, sym);
STATIC_ASSERTION_SIZE(_callback_t, sym, 0);
/**
 * @brief the actual data type for a callback vector
 **/
struct _lang_prop_callback_vector_t {
	lang_prop_callback_vector_t* prev;      /*!< previous pointer the global linked list */
	lang_prop_callback_vector_t* next;      /*!< next pointer the global linked list */
	const lang_bytecode_table_t* bc_table;  /*!< the associcated bytecode table */
	size_t capacity;                        /*!< the capacity of the callback list */
	size_t count;                           /*!< the number of callback functions */
	_callback_t** callbacks;                /*!< the callback list */
};

/**
 * @brief the callback definitions
 **/
static vector_t* _callback_defs = NULL;

/**
 * @brief the global list for all existing callback vectors
 **/
static lang_prop_callback_vector_t* _callback_objs = NULL;

/**
 * @brief parse the symbol prefix to the synbol array
 * @param bc_table the bytecode table instance
 * @param callback the callback definition
 * @param symbol_ids the result buffer for the symbol array
 * @param buffer_size the size (in number of elements) of the result buffer
 * @return the number of symbol ids has been output, 0 indicates that this symbol is not referenced by the bytecode table
 **/
static inline uint32_t _parse_symbol(const lang_bytecode_table_t* bc_table, const lang_prop_callback_t* callback, uint32_t* symbol_ids, size_t buffer_size)
{
	static char buffer[4096];
	uint32_t nsymbol = 0;
	const char* ptr;
	char* out_ptr = buffer;
	size_t sz = sizeof(buffer) - 1;
	int warnned = 0;
	for(ptr = callback->symbol_prefix;;)
	{
		if(*ptr == 0 || *ptr == '.')
		{
			size_t symbol_length = (size_t)(out_ptr - buffer);
			if(symbol_length > 0)
			{
				*out_ptr = 0;
				uint32_t symbol_id = lang_bytecode_table_get_string_id(bc_table, buffer);
				if(ERROR_CODE(uint32_t) == symbol_id)
				{
					LOG_DEBUG("Skip symbol %s because it's not actualled used", callback->symbol_prefix);
					return 0;
				}
				if(buffer_size > 0)
				    symbol_ids[nsymbol ++] = symbol_id, buffer_size --;
				else
				    LOG_WARNING("symbol contains too many sections, truncated");
				sz = sizeof(buffer) - 1;
				out_ptr = buffer;
			}
			if(*ptr == 0) break;
			else ptr ++;
		}
		if(sz > 0)
		    *(out_ptr++) = *(ptr++), sz --;
		else if(!warnned)
		{
			LOG_WARNING("symbol truncated");
			warnned = 1;
		}
	}
	return nsymbol;
}

/**
 * @brief create a new callback object in the callback vector
 * @param bc_table the bytecode table that this callback function will be used for
 * @param callback the callback definition that made this callback object
 * @param result the result buffer
 * @return the number of callback object has been created or error code
 **/
static inline int _callback_obj_new(const lang_bytecode_table_t* bc_table, const lang_prop_callback_t* callback, _callback_t** result)
{
	static uint32_t symbol[4096];
	uint32_t nsection;
	*result = NULL;

	if(ERROR_CODE(uint32_t) == (nsection = _parse_symbol(bc_table, callback, symbol, sizeof(symbol) / sizeof(symbol[0]))))
	{
		LOG_WARNING("Error when getting the symbol from the bytecode table");
		return 0;
	}

	if(nsection == 0)
	{
		LOG_DEBUG("Skip unused property %s", callback->symbol_prefix);
		return 0;
	}

	_callback_t* cb = (_callback_t*)malloc(sizeof(_callback_t) + sizeof(uint32_t) * nsection);

	if(NULL == cb) ERROR_RETURN_LOG(int, "Cannot allocate memory for the callback function");

	cb->get = callback->get;
	cb->set = callback->set;
	cb->param = callback->param;
	cb->num_sections = nsection;
	memcpy(cb->sym, symbol, sizeof(uint32_t) * nsection);

	*result = cb;
	return 1;
}

/**
 * @brief the compare function used to compare two callback object
 * @param l the left operand
 * @param r the right operand
 * @return the result
 **/
static inline int _compare(const void* l, const void* r)
{
	const _callback_t* left = *(const _callback_t**)l;
	const _callback_t* right = *(const _callback_t**)r;

	uint32_t i;
	for(i = 0;left->num_sections > i && right->num_sections > i; i ++)
	    if(left->sym[i] > right->sym[i]) return 1;
	    else if(left->sym[i] < right->sym[i]) return -1;

	if(left->num_sections > right->num_sections) return 1;
	else if(left->num_sections < right->num_sections) return -1;

	return 0;
}

/**
 * @brief append a new callback object to the callback vector
 * @param vector the target callback vector
 * @param callback the callback definition
 * @return status code
 **/
static inline int _callback_vector_append(lang_prop_callback_vector_t* vector, const lang_prop_callback_t* callback)
{
	_callback_t* cb;
	int rc = _callback_obj_new(vector->bc_table, callback, &cb);

	if(rc == ERROR_CODE(int)) ERROR_RETURN_LOG(int, "Cannot create callback object");

	if(rc == 0) return 0;

	if(vector->capacity <= vector->count)
	{
		LOG_TRACE("The callback vector is full, try to resize to size %zu", vector->capacity * 2);

		_callback_t** new_list = (_callback_t**)realloc(vector->callbacks, vector->capacity * 2 * sizeof(_callback_t*));
		if(NULL == new_list)
		{
			free(cb);
			ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the callback vector");
		}

		vector->callbacks = new_list;
		vector->capacity *= 2;
	}

	/* Do a insersion sort */
	//vector->callbacks[vector->count ++] = cb;
	uint32_t i;
	for(i = 0; i < vector->count; i ++)
	{
		int rc = _compare(&cb, vector->callbacks + i);
		if(rc == 0)
		{
			free(cb);
			ERROR_RETURN_LOG(int, "Duplicated handler");
		}
		if(rc < 0) break;
	}

	if(i < vector->count) memmove(vector->callbacks + i + 1, vector->callbacks + i, sizeof(_callback_t*) * (vector->count - i));

	vector->callbacks[i] = cb;
	vector->count ++;

	LOG_DEBUG("New callback handler %s has been appended to existing callback vector @%p", callback->symbol_prefix, vector);

	return 0;
}


int lang_prop_init()
{
	if(NULL == (_callback_defs = vector_new(sizeof(lang_prop_callback_t), LANG_PROP_CALLBACK_VEC_INIT_SIZE)))
	    ERROR_RETURN_LOG(int, "Cannot create the callback def table for the property table");

	return 0;
}

int lang_prop_finalize()
{
	int rc = 0;
	if(NULL != _callback_defs && ERROR_CODE(int) == vector_free(_callback_defs))
	    rc = -1;
	return rc;
}

int lang_prop_register_callback(const lang_prop_callback_t* callback)
{
	if(NULL == _callback_defs) ERROR_RETURN_LOG(int, "Cannot modify a intialized property callback list");
	if(NULL == callback || NULL == callback->symbol_prefix) ERROR_RETURN_LOG(int, "Invalid arguments");

	vector_t* new = vector_append(_callback_defs, callback);
	if(NULL == new) ERROR_RETURN_LOG(int, "Cannot append the new callback function to the callback def table");

	_callback_defs = new;

	lang_prop_callback_vector_t* vec;
	for(vec = _callback_objs; NULL != vec; vec = vec->next)
	    if(ERROR_CODE(int) == _callback_vector_append(vec, callback))
	        LOG_WARNING("Cannot append the callback hook for symbol prefix %s to the callback vector @%p", callback->symbol_prefix, vec);
	return 0;
}

int lang_prop_callback_vector_free(lang_prop_callback_vector_t* vector)
{
	if(NULL == vector)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	size_t i;

	if(NULL != vector->callbacks)
	{
		for(i = 0; i < vector->count; i ++)
		    free(vector->callbacks[i]);
		free(vector->callbacks);
	}

	if(vector->prev == NULL) _callback_objs = vector->next;
	else vector->prev->next = vector->next;
	if(vector->next != NULL) vector->next->prev = vector->prev;

	free(vector);
	return 0;
}

lang_prop_callback_vector_t* lang_prop_callback_vector_new(const lang_bytecode_table_t* bc_table)
{
	/* We should make sure that the initial size contains at least 32 elements */
	size_t ncallbacks = vector_length(_callback_defs);
	if(ncallbacks < 32) ncallbacks = 32;

	size_t size = sizeof(lang_prop_callback_vector_t);
	size_t callback_size = sizeof(_callback_t*) * ncallbacks;

	lang_prop_callback_vector_t* ret = (lang_prop_callback_vector_t*)calloc(1, size);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the callback vector");

	ret->callbacks = (_callback_t**)calloc(1, callback_size);
	if(NULL == ret->callbacks) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the callback list");

	size_t i;
	_callback_t* cb = NULL;
	ret->bc_table = bc_table;
	ret->count = 0;
	ret->capacity = ncallbacks;

	for(i = 0; i < vector_length(_callback_defs); i ++)
	{
		const lang_prop_callback_t* current = VECTOR_GET_CONST(lang_prop_callback_t, _callback_defs, i);
		if(NULL != current)
		{
			int rc = _callback_obj_new(bc_table, current, &cb);
			if(rc == ERROR_CODE(int))
			    ERROR_LOG_GOTO(ITER_ERR, "Cannot create the callback object");
			else if(rc == 1)
			    ret->callbacks[ret->count ++] = cb;
			continue;
ITER_ERR:
			if(cb != NULL) free(cb);
			goto ERR;
		}
		else LOG_WARNING("Cannot read the callback definition vector");
	}

	qsort(ret->callbacks, ret->count, sizeof(_callback_t*), _compare);

	for(i = 1; i < ret->count; i ++)
	    if(_compare(&ret->callbacks[i-1], &ret->callbacks[i]) == 0)
	        ERROR_LOG_GOTO(ERR, "Duplicated handler");

	LOG_DEBUG("Callback vector has been successfully created with %zu entries", ret->count);

	ret->next = _callback_objs;
	ret->prev = NULL;

	if(NULL != _callback_objs) _callback_objs->prev = ret;
	_callback_objs = ret;

	return ret;
ERR:
	if(NULL != ret)
	{
		size_t i;
		if(ret->callbacks != NULL)
		{
			for(i = 0; i < ret->count; ret ++)
			    free(ret->callbacks[i]);
			free(ret->callbacks);
		}
	}
	return NULL;
}

/**
 * @brief perform a binary search on a callback object array
 * @param arr the callback array
 * @param n the n-th symbol section to compare
 * @param target the target symbol id
 * @param begin the reference to the variable for left-most range
 * @param end the reference to the variable for the right-most range
 * @return status code
 **/
static inline int _bsearch(_callback_t const* const* arr, uint32_t n, uint32_t target, size_t* begin, size_t* end)
{
	size_t l = *begin, r = *end;
	if(n >= arr[l]->num_sections) l ++;
	if(arr[l]->sym[n] < target)
	{
		while(r - l > 1)
		{
			size_t m = (l + r) / 2;
			if(target <= arr[m]->sym[n]) r = m;
			else l = m;
		}
		if(r == *end || arr[r]->sym[n] != target) return 0;
		*begin = r;
	} else if(target < arr[l]->sym[n]) return 0;

	l = *begin, r = *end;
	while(r - l > 1)
	{
		size_t m = (l + r) / 2;
		if(target < arr[m]->sym[n]) r = m;
		else l = m;
	}
	*end = r;
	return 1;
}

/**
 * @brief match a symbol in the callback vector
 * @param callbacks the callback vector
 * @param sym the symbol aray
 * @param nsec the sections in the symbol list
 * @param result the result buffer
 * @return the nubmer of entity has been found, or error code
 **/
static inline int _match_symbol(const lang_prop_callback_vector_t* callbacks, const uint32_t* sym, uint32_t nsec, _callback_t const * * result)
{
	*result = NULL;

	size_t begin = 0, end = callbacks->count;

	uint32_t i;

	if(end == 0) return 0;
	for(i = 0; i < nsec && end - begin > 1; i ++)
	{
		size_t next_begin = begin, next_end = end;
		if(_bsearch((_callback_t const * const *)callbacks->callbacks, i, sym[i], &next_begin, &next_end))
		    begin = next_begin, end = next_end;
		else
		    break;
	}

	if(end - begin > 0)
	{
		*result = callbacks->callbacks[begin];
		for(i = 0; i < (*result)->num_sections; i ++)
		    if(i >= nsec || (*result)->sym[i] != sym[i]) return 0;
		return 1;
	}
	return 0;
}

/**
 * @brief convert a symbol from smybol id to symbol array
 * @param callbacks the callback vectors
 * @param symid the symbol id
 * @param symbuf the symbol id array buffer
 * @param nsecbuf the buffer used to return how many sections
 * @return status code
 **/
static inline int _get_symbol_array(const lang_prop_callback_vector_t* callbacks, uint32_t symid, uint32_t const * * symbuf, uint32_t* nsecbuf)
{
	lang_bytecode_operand_id_t id = {
		.type = LANG_BYTECODE_OPERAND_SYM,
		.id = symid
	};

	*nsecbuf = lang_bytecode_table_sym_id_length(callbacks->bc_table, id);
	if(ERROR_CODE(uint32_t) == *nsecbuf) ERROR_RETURN_LOG(int, "Cannot get the length of the symbol");
	*symbuf = lang_bytecode_table_sym_id_to_strid_array(callbacks->bc_table, id);
	if(NULL == *symbuf) ERROR_RETURN_LOG(int, "Cannot read the symbol from the byteocde table");
	return 0;
}
int lang_prop_get(const lang_prop_callback_vector_t* callbacks, uint32_t symid, lang_prop_type_t* type, lang_prop_value_t* buffer)
{
	if(NULL == callbacks || NULL == type || ERROR_CODE(uint32_t) == symid) ERROR_RETURN_LOG(int, "Invalid arguments");

	const _callback_t* cb;
	const uint32_t* sym;
	uint32_t nsec;
	if(ERROR_CODE(int) == _get_symbol_array(callbacks, symid, &sym, &nsec))
	    ERROR_RETURN_LOG(int, "Cannot read symbol");

	int rc = _match_symbol(callbacks, sym, nsec, &cb);
	if(ERROR_CODE(int) == rc)
	    ERROR_RETURN_LOG(int, "Error during mathcing symbol");

	if(0 == rc)
	{
		LOG_DEBUG("Cannot find the symbol");
		return 0;
	}

	if(NULL == cb->get)
	{
		LOG_DEBUG("No handler to get this property");
		return 0;
	}

	for(*type = (lang_prop_type_t)0; *type < LANG_PROP_TYPE_COUNT; (*type) ++)
	{
		int rc = cb->get(callbacks, cb->param, nsec - cb->num_sections, sym + cb->num_sections, *type, buffer);
		if(rc == 1) return 1;
		if(ERROR_CODE(int) == rc) ERROR_RETURN_LOG(int, "Error during executing the getter");
	}

	return 0;
}
int lang_prop_set(const lang_prop_callback_vector_t* callbacks, uint32_t symid, lang_prop_type_t type, const void* buffer)
{
	if(callbacks == NULL || ERROR_CODE(uint32_t) == symid || NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");

	const _callback_t* cb;
	const uint32_t* sym;
	uint32_t nsec;

	if(ERROR_CODE(int) == _get_symbol_array(callbacks, symid, &sym, &nsec))
	    ERROR_RETURN_LOG(int, "Cannot read symbol");

	int rc = _match_symbol(callbacks, sym, nsec, &cb);
	if(ERROR_CODE(int) == rc)
	    ERROR_RETURN_LOG(int, "Error during mathcing symbol");

	if(0 == rc)
	{
		LOG_DEBUG("Cannot find the symbol");
		return 0;
	}

	if(NULL == cb->set)
	{
		LOG_DEBUG("No handler to set this property");
		return 0;
	}

	return cb->set(callbacks, cb->param, nsec - cb->num_sections, sym + cb->num_sections, type, buffer);
}

const char* lang_prop_get_symbol_string(const lang_prop_callback_vector_t* callbacks, uint32_t strid)
{
	if(NULL == callbacks || NULL == callbacks->bc_table || ERROR_CODE(uint32_t) == strid)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	lang_bytecode_operand_id_t id = {
		.type = LANG_BYTECODE_OPERAND_STR,
		.id = strid
	};

	return lang_bytecode_table_str_id_to_string(callbacks->bc_table, id);
}
