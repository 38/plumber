/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief Define common binary operations on the field
 * @file psnl/include/psnl/cpu/binops.h
 **/
#ifndef __PSNL_CPU_BINOPS_H__
#define __PSNL_CPU_BINOPS_H__

/**
 * @brief The operation we supported
 **/
typedef enum {
	PSNL_CPU_BINOPS_OPCODE_ADD,      /*!< The addition of a field */
	PSNL_CPU_BINOPS_OPCODE_SUB,      /*!< The substraction of the field */ 
	PSNL_CPU_BINOPS_OPCODE_MUL       /*!< The field multiplication, NOTE: this is not the matrix multiplicatoin */
} psnl_cpu_binops_opcode_t;

/**
 * @brief Create a new binary operation from the given RHS
 * @param op The operation we want to perform
 * @param first The first operand
 * @param second  The second operand
 * @return The newly created continuation object for this operation
 **/
psnl_cpu_field_cont_t* psnl_cpu_binops_new(psnl_cpu_binops_opcode_t op, const psnl_cpu_rhs_desc_t* first, const psnl_cpu_rhs_desc_t* second);

#endif /* __PSNL_CPU_BINOPS_H__ */
