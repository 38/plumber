/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The abstract expression represetation
 * @file include/psnl/expr.h
 **/
#ifndef __PSNL_EXPR_H__
#define __PSNL_EXPR_H__

/**
 * @brief an PSNL expression
 **/
typedef struct _psnl_expr_t psnl_expr_t;


/**
 * @brief Indicates which kind of device we want to run the expression
 **/
typedef enum {
	PSNL_EXPR_DEVICE_CPU   /*!< The expression should be run on CPU */
} psnl_expr_device_t;

/**
 * @brief Indicates what operation we should performe
 **/
typedef enum {
	PSNL_EXPR_OP_MAT_MUL    /*!< Performe the matrix multiplication */
} psnl_expr_op_t;

/**
 * @brief The type of a expression  operand
 **/
typedef enum {
	PSNL_EXPR_OPERAND_TYPE_EXPR,   /*!< If the operand is another expression */
	PSNL_EXPR_OPERAND_TYPE_FIELD   /*!< If the operand is a field */
} psnl_expr_operand_type_t;

/**
 * @brief The operand expression operand
 **/
typedef struct {
	psnl_expr_operand_type_t kind;      /*!< Which kind of operand we are using */
	union {
		psnl_cpu_field_t*    cpu_field; /*!< The CPU field */
		/* TODO: Add GPU field support */
		psnl_expr_t*         expr;      /*!< The operand is another expression */
	}                        data;      /*!< The actual data type */
} psnl_expr_operand_t;

/**
 * @brief Execute the operation at the give position
 * @param oper The operands array
 * @param pos Cooridinate to compute
 * @param result The result buffer
 * @note This function is used for the continuation that can be run by CPU
 * @return status code
 **/
typedef int (*psnl_expr_exec_at_cb_t)(const psnl_expr_operand_t* oper, int32_t* pos, void* result);

/**
 * @brief Create a new expression
 * @param op The opcode
 * @param oper The operands
 * @param res_elem_size The result element size in bytes
 * @param cpu_exec If given it means this conintuation can be run on CPU directly
 * @param range The valid range of this continuation
 * @return the newly created expression
 **/
psnl_expr_t* psnl_expr_new(psnl_expr_op_t op, psnl_expr_operand_t* oper, const psnl_dim_t* range, size_t res_elem_size, psnl_expr_exec_at_cb_t cpu_exec);

/**
 * @brief Dispose a use expression
 * @param expr The expression to dispose
 * @return status code
 **/
int psnl_expr_free(psnl_expr_t* expr);

/**
 * @brief Commit the expression to RLS
 * @param expr The expression
 * @return the token or error code
 **/
scope_token_t psnl_expr_commit(psnl_expr_t* expr);

/**
 * @brief Increase the reference counter of the expression
 * @param expr The expression to dispose
 * @return status code
 **/
int psnl_expr_incref(const psnl_expr_t* expr);

/**
 * @brief Decrease the rence counter for the expression
 * @param expr The expression to dispose
 * @return status code
 **/
int psnl_expr_decref(const psnl_expr_t* expr);

/**
 * @brief Compute the given expression on CPU (if possible)
 * @param expr The expression
 * @param buffer The result buffer
 * @return status code
 **/
int psnl_expr_compute(const psnl_expr_t* expr, void* buffer);

#if 0
//TODO: desgin the API
int psnl_expr_traverse(const psnl_expr_t* expr, 
#endif

#endif /* __PSNL_EXPR_H__ */
