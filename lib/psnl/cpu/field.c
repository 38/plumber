/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <psnl/dim.h>
#include <psnl/cpu/field.h>

/**
 * @brief The actual data structure used for the Field lives on the CPU
 **/
struct _psnl_cpu_field_t {
	size_t               elem_size;        /*!< The element size */
	pstd_scope_gc_obj_t* gc_obj;           /*!< The GC object for this field */
	uintpad_t            __padding__[0];
	psnl_dim_t           dim[0];           /*!< The dimensional data */
	char                 data[0];          /*!< The actual data section */
};
STATIC_ASSERTION_SIZE(psnl_cpu_field_t, dim, 0);
STATIC_ASSERTION_SIZE(psnl_cpu_field_t, data, 0);
STATIC_ASSERTION_LAST(psnl_cpu_field_t, dim);
STATIC_ASSERTION_LAST(psnl_cpu_field_t, data);

/**
 * @brief Reperesent the string representation of a field
 * @todo  Do we need make the stream able to output seralized field?
 **/
typedef struct {
	const void* data;   /*!< The actual data to be written */
	size_t      size;   /*!< The size of the struct */
} _stream_t;

/**
 * @brief Get the padded size from the original one
 * @param size The size of the padded size
 * @return the padded size
 **/
static inline size_t _get_padded_size(size_t size) 
{
	size_t rem = size % sizeof(uintpad_t);

	return rem > 0 ? size - rem + sizeof(uintpad_t) : size;
}

static int _field_data_free(void* obj)
{
	/* TODO(field memory pool): cache the memory we are using at this point would be really helpful */
	free(obj);
	return 0;
}

psnl_cpu_field_t* psnl_cpu_field_new(const psnl_dim_t* dim, size_t elem_size)
{
	if(NULL == dim || elem_size == 0)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t size = sizeof(psnl_cpu_field_t) + psnl_dim_space_size(dim) * elem_size + _get_padded_size(psnl_dim_data_size(dim));

	/* TODO(field memory pool): even though we just allocated from OS at this time, it should be better if we can cache the meomry we are using here */
	psnl_cpu_field_t* ret = (psnl_cpu_field_t*)malloc(size);

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new CPU field");

	memcpy(ret->dim, dim, psnl_dim_data_size(dim));
	ret->elem_size = elem_size;
	ret->gc_obj = NULL;

	return ret;
}


int psnl_cpu_field_free(psnl_cpu_field_t* field)
{
	if(NULL == field)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(field->gc_obj != NULL)
		ERROR_RETURN_LOG(int, "Refuse to dispose a committed RLS object");

	return _field_data_free(field);
}

int psnl_cpu_field_incref(const psnl_cpu_field_t* field)
{
	return pstd_scope_gc_incref(field->gc_obj);
}

int psnl_cpu_field_decref(const psnl_cpu_field_t* field)
{
	return pstd_scope_gc_decref(field->gc_obj);
}

static void* _open(const void* mem)
{
	_stream_t* ret = pstd_mempool_alloc(sizeof(_stream_t));

	if(NULL == ret)
		ERROR_PTR_RETURN_LOG("Cannot allocate memory for the stream object");
	
	const psnl_cpu_field_t* data = (const psnl_cpu_field_t*)mem;
	
	ret->size = _get_padded_size(psnl_dim_data_size(data->dim)) + psnl_dim_data_size(data->dim) * data->elem_size;

	ret->data = mem;

	return ret;
}

static int _close(void* stream_mem)
{
	return pstd_mempool_free(stream_mem);
}

static int _eos(const void* stream_mem)
{
	const _stream_t* stream = (const _stream_t*)stream_mem;

	return stream->size > 0;
}

static size_t _read(void* __restrict stream_mem, void* __restrict buf, size_t count)
{
	_stream_t* stream = (_stream_t*)stream_mem;

	size_t ret = count;
	if(ret > stream->size) ret = stream->size;

	memcpy(buf, stream->data, ret);
	
	stream->size -= ret;
	stream->data = (const void*)(((const uint8_t*)stream->data) + ret);
	return ret;
}

scope_token_t psnl_cpu_field_commit(psnl_cpu_field_t* field)
{
	if(NULL == field)
		ERROR_RETURN_LOG(scope_token_t, "Invalid arguments");

	if(field->gc_obj != NULL)
		ERROR_RETURN_LOG(scope_token_t, "Cannot re-committed a token that is already in RLS");

	scope_entity_t ent = {
		.data = field,
		.free_func = _field_data_free,
		.open_func = _open,
		.close_func = _close,
		.eos_func   = _eos,
		.read_func  = _read
	};

	return pstd_scope_gc_add(&ent, &field->gc_obj);
}

void* psnl_cpu_field_get_data(psnl_cpu_field_t* field, psnl_dim_t const ** dim_buf)
{
	if(NULL == field)
		ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(NULL != dim_buf) 
		*dim_buf = field->dim;

	return field->data + _get_padded_size(psnl_dim_data_size(field->dim));
}

const void* psnl_cpu_field_get_data_const(const psnl_cpu_field_t* field, psnl_dim_t const** dim_buf)
{
	if(NULL == field)
		ERROR_PTR_RETURN_LOG("Invalid arguments");
	
	if(NULL != dim_buf) 
		*dim_buf = field->dim;

	return field->data + _get_padded_size(psnl_dim_data_size(field->dim));
}

const psnl_cpu_field_t* psnl_cpu_field_from_rls(scope_token_t token)
{
	if(ERROR_CODE(scope_token_t) == token)
		ERROR_PTR_RETURN_LOG("Invlaid arguments");

	pstd_scope_gc_obj_t* gc_obj = pstd_scope_gc_get(token);

	return gc_obj == NULL ? NULL : gc_obj->obj;
}

/**
 * @brief The map from the cell type to the field type
 **/
static const char* _type_map[] = {
	[PSNL_CPU_FIELD_CELL_TYPE_DOUBLE] = "plumber/std/numeric/DoubleField"
};
STATIC_ASSERTION_EQ(PSNL_CPU_FIELD_CELL_TYPE_COUNT, sizeof(_type_map) / sizeof(_type_map[0]));

int psnl_cpu_field_type_parse(const char* type_name, psnl_cpu_field_type_info_t* buf)
{
	if(NULL == type_name || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	const char* major_begin = type_name;
	const char* major_end   = type_name;

	for(;*major_end != 0 && *major_end != ' ' && *major_end != '\t'; major_end ++);

	uint32_t type_code;
	for(type_code = 0; _type_map[type_code] && 
			           !(major_begin + strlen(_type_map[type_code]) == major_end &&
					     0 == memcmp(major_begin, _type_map[type_code], (size_t)(major_end - major_begin)));
		type_code ++);

	if(_type_map[type_code] == NULL)
		ERROR_RETURN_LOG(int, "Unknown field type: %s", type_name);

	buf->cell_type = (psnl_cpu_field_cell_type_t)type_code;

	switch(buf->cell_type)
	{
		case PSNL_CPU_FIELD_CELL_TYPE_DOUBLE:
			buf->cell_size = sizeof(double);
			break;

		default:
			ERROR_RETURN_LOG(int, "Unknown type code");
	}

	/* Then we need to parse the dimension */

	const char* dim_start = major_end;

	buf->n_dim = 0;

	for(;*dim_start != 0 && (*dim_start == ' ' || *dim_start == '\t'); dim_start ++);

	if(*dim_start != 0 && memcmp(dim_start, "@dim(", 5) == 0)
	{
		char* next = NULL;

		long long dim_val = strtoll(dim_start + 5, &next, 0);

		if(NULL == next || *next != ')')
			ERROR_RETURN_LOG(int, "Invalid dimension description");

		if(dim_val <= 0 || dim_val > 0xffffffffll)
			ERROR_RETURN_LOG(int, "Invalid dimension value");

		buf->n_dim = (uint32_t)dim_val;
	}

	return 0;
}

int psnl_cpu_field_type_dump(const psnl_cpu_field_type_info_t* info, char* buf, size_t buf_size)
{
	if(NULL == info || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

#ifndef __clang__
	if(info->cell_type < 0 || info->cell_type >= PSNL_CPU_FIELD_CELL_TYPE_COUNT)
#else
	if(info->cell_type >= PSNL_CPU_FIELD_CELL_TYPE_COUNT)
#endif
		ERROR_RETURN_LOG(int, "Invalid arguments: cell type code");

	size_t bytes_needed = 0;

	if(info->n_dim > 0)
		bytes_needed = (size_t)snprintf(buf, buf_size, "%s @dim(%u)", _type_map[info->cell_type], info->n_dim);
	else
		bytes_needed = (size_t)snprintf(buf, buf_size, "%s", _type_map[info->cell_type]);

	if(bytes_needed > buf_size)
		ERROR_RETURN_LOG(int, "The output buffer is too small");

	return 0;
}
