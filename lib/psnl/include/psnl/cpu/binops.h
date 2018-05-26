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
 * @brief Describe a single LHS
 * @note Only one of the field should be non-null value
 **/
typedef struct {

	enum {
		PSNL_CPU_BINOPS_LHS_SCALAR,      /*!< The LHS is a scalar */
		PSNL_CPU_BINOPS_LHS_FIELD,       /*!< The LHS is a field */
		PSNL_CPU_BINOPS_LHS_CONT         /*!< The LHS is another continuation */
	}                       type;        /*!< The type of the LHS */

	union {
		double                  scalar;  /*!< The LHS is a scalar */
		psnl_cpu_field_t*       field;   /*!< The field typed LHS */
		psnl_cpu_field_cont_t*  cont;    /*!< The continuation typed LHS */
	} value;                             /*!< The actual value of the field */
} psnl_cpu_binops_lhs_t;

/**
 * @brief The operation we supported
 **/
typedef enum {
	PSNL_CPU_BINOPS_OPCODE_ADD,      /*!< The addition of a field */
	PSNL_CPU_BINOPS_OPCODE_SUB,      /*!< The substraction of the field */ 
	PSNL_CPU_BINOPS_OPCODE_MUL       /*!< The field multiplication, NOTE: this is not the matrix multiplicatoin */
} psnl_cpu_binops_opcode_t;

/**
 * @brief Create a new binary operation from the given LHS
 * @param op The operation we want to perform
 * @param a  The first operand
 * @param b  The second operand
 * @return The newly created continuation object for this operation
 **/
psnl_cpu_field_cont_t* psnl_cpu_binops_new(psnl_cpu_binops_opcode_t op, psnl_cpu_binops_lhs_t a, psnl_cpu_binops_lhs_t b);

#endif /* __PSNL_CPU_BINOPS_H__ */
