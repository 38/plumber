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
#include <psnl/cpu/binops.h>

/**
 * @brief The actual data structure for a binary operator RHS
 **/
typedef struct {
	psnl_cpu_rhs_t first;   /*!< First operand */
	psnl_cpu_rhs_t second;  /*!< Second operand */
} _binary_rhs_t;

psnl_cpu_field_cont_t* psnl_cpu_binops_new(psnl_cpu_binops_opcode_t op, const psnl_cpu_rhs_desc_t* first, const psnl_cpu_rhs_desc_t* second)
{
	_binary_rhs_t rhs;

	if(ERROR_CODE(int) == psnl_cpu_rhs_init(first, &rhs.first))
		ERROR_PTR_RETURN_LOG("Cannot create the first RHS");

	if(ERROR_CODE(int) == psnl_cpu_rhs_init(second, &rhs.second))
		ERROR_PTR_RETURN_LOG("Cannot create the second RHS");

	/* TODO: create a new continuation */

	(void)op;


	return NULL;
}
