/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <pstd.h>

#include <psnl/dim.h>
#include <psnl/cpu/field.h>
#include <psnl/cpu/field_cont.h>
#include <psnl/cpu/rhs.h>


static int _read_field_lhs(const int32_t* pos, const void* lhs_mem, void * __restrict buf)
{
	const _field_lhs_t* lhs = (const _field_lhs_t*)lhs_mem;

	memcpy(buf, ((int8_t*)lhs->buf) + lhs->elem_size * psnl_dim_get_offset(lhs->dim_data, pos) * lhs->elem_size, lhs->elem_size);

	return 0;
}

static int _free_field_lhs(const void* lhs_mem)
{
	const _field_lhs_t* lhs = (const _field_lhs_t*)lhs_mem;

	return psnl_cpu_field_decref(lhs);
}

static int _read_cont_lhs(const int32_t* pos, const void* lhs_mem, void * __restrict buf)
{
	const _cont_lhs_t* lhs = (const _cont_lhs_t*)lhs_mem;

	return psnl_cpu_field_cont_value_at(lhs->cont, pos, buf);
}

static int _free_cont_lhs(const void* lhs_mem)
{
	const _cont_lhs_t* lhs = (_cont_lhs_t*)lhs_mem;

	return psnl_cpu_field_cont_decref(lhs);
}

static int _read_scalar_lhs(const int32_t* pos, const void* lhs_mem, void * __restrict buf)
{
	(void)pos;

	/* TODO: what if we support single precision value later, should type conversion be another cont ? seems yes */
	const _scalar_lhs_t* lhs = (_cont_lhs_t*)lhs_mem;

	memcpy(buf, &lhs->value, sizeof(double));

	return 0;
}

int psnl_cpu_rhs_init_field(const psnl_cpu_field_t* field, size_t elem_size, psnl_cpu_rhs_t* buf)
{
	if(NULL == field || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	buf->read_func = _read_field_lhs;
	buf->free_func = _free_field_lhs;

	buf->field.field = field;

	buf->field.elem_size = elem_size;

	if(NULL == (buf->field.data_buf = psnl_cpu_field_get_data_const(field, &buf->field.dim_data)))
		ERROR_RETURN_LOG(int, "Cannot get the data section of the field");

	return 0;
}

int psnl_cpu_rhs_init_cont(const psnl_cpu_field_cont_t* cont, psnl_cpu_rhs_t* buf)
{
	if(NULL == field || NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	buf->read_func = _read_cont_lhs;
	buf->free_func = _free_cont_lhs;

	buf->cont.cont = cont;

	return 0;
}

int psnl_cpu_rhs_init_scalar(double value, psnl_cpu_rhs_t* buf)
{
	if(NULL == buf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	buf->read_func = _read_scalar_lhs;

	buf->scalar.value = value;

	return 0;
}
