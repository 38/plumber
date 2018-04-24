/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The continuation for a field lives on CPU
 * @file include/psnl/cpu/cont
 * @note This type should be mapped to following type:
 *             plumber/numeric/Computation plumber/numeric/Field $T
 **/
#ifndef __PSNL_CPU_FIELD_CONT_H__
#define __PSNL_CPU_FIELD_CONT_H__

/**
 * @brief The continuation that lives on the CPU
 **/
typedef struct _psnl_cpu_field_cont_t psnl_cpu_field_cont_t;

/**
 * @brief Represent a continuation on a field
 * @param ndim The number of dimension
 * @param pos  The position
 * @param lhs  The left hand side, which can be either another continuation or can be a field
 * @param rhs  The right hand side
 * @return nothing
 **/
typedef void (*psnl_cpu_field_cont_eval_func_t)(const int32_t* pos, const void* __restrict lhs, void* __restrict rhs);

/**
 * @brief Called when the continuation needs to be freed
 * @param lhs The left-hand-side data
 * @return status code
 **/
typedef int (*psnl_cpu_field_cont_free_func_t)(const void* __restrict lhs);

/**
 * @brief Describe a continuation that lives on the CPU
 **/
typedef struct {
	const psnl_dim_t*                 range; /*!< The valid range of the result field of this continuation */
	const void* __restrict            lhs;   /*!< The pointer to the left-hand-side */
	psnl_cpu_field_cont_eval_func_t   eval;  /*!< What we need to do to evaluate the value at that point */
	psnl_cpu_field_cont_free_func_t   free;  /*!< What we need to do to dispose the LHS */
	uint64_t                          tag;   /*!< Reserved field: might be useful when we want to make a JIT optimizer for continuation */
} psnl_cpu_field_cont_desc_t;

/**
 * @brief A continuation on CPU
 * @param dim The dimension specification of this field
 * @param func The function for this continuation
 * @param lhs The left-hand-side
 * @return The newly created CPU continuation
 **/
psnl_cpu_field_cont_t* psnl_cpu_field_cont_new(const psnl_cpu_field_cont_desc_t* cont_desc);

/**
 * @brief Dispose a used continuation
 * @param cont The continuation to dispose
 * @return status code 
 **/
int psnl_cpu_field_cont_free(psnl_cpu_field_cont_t* cont);

/**
 * @brief Increase the reference counter of the CPU continuation
 * @param cont The continuation we want to incref
 * @return status code
 **/
int psnl_cpu_field_cont_incref(const psnl_cpu_field_cont_t* cont);

/**
 * @brief Decrease the reference counter  for a CPU continuation
 * @param cont The contuation
 * @return status code
 **/
int psnl_cpu_field_cont_decref(const psnl_cpu_field_cont_t* cont);

/**
 * @brief Commit the CPU continuation to the CPU scope
 * @param cont The continuation
 * @return token The RLS token for this continuation
 **/
scope_token_t psnl_cpu_field_cont_commit(psnl_cpu_field_cont_t* cont);

/**
 * @brief Evaluate the continuation at the given position
 * @param cont The continuation
 * @param ndim The n-th dimension
 * @param pos The position
 * @return status code
 **/
int psnl_cpu_field_cont_value_at(const psnl_cpu_field_cont_t* cont, uint32_t ndim, int32_t* __restrict pos, void* __restrict buf);

#endif /* __PSNL_CPU_FIELD_CONT_H__ */
