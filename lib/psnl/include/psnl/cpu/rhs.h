/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The standard continuation RHS representation
 * @file psnl/include/psnl/cpu/rhs.h
 **/

#ifndef __PSNL_CPU_RHS_H__
#define __PSNL_CPU_RHS_H__

/**
 * @brief Describe a single RHS
 * @note Only one of the field should be non-null value
 **/
typedef struct {
	enum {
		PSNL_CPU_RHS_SCALAR,      /*!< The RHS is a scalar */
		PSNL_CPU_RHS_FIELD,       /*!< The RHS is a field */
		PSNL_CPU_RHS_CONT         /*!< The RHS is another continuation */
	}                       type;        /*!< The type of the RHS */

	size_t                  elem_size;   /*!< The size of each cell in the field */

	union {
		double                  scalar;  /*!< The RHS is a scalar */
		psnl_cpu_field_t*       field;   /*!< The field typed RHS */
		psnl_cpu_field_cont_t*  cont;    /*!< The continuation typed RHS */
	} value;                             /*!< The actual value of the field */
} psnl_cpu_rhs_desc_t;

/**
 * @brief The RHS which is a field
 **/
typedef struct {
	size_t                  elem_size;  /*!< The size of the element */
	const void*             data_buf;   /*!< The actual data section */
	const psnl_dim_t*       dim_data;   /*!< The dimensional data */
	const psnl_cpu_field_t* field;      /*!< The field object that holds the data buffer */
} psnl_cpu_rhs_field_t;

/**
 * @brief The continuation RHS
 **/
typedef struct {
	const psnl_cpu_field_cont_t*  cont;  /*!< The continuation object */
} psnl_cpu_rhs_cont_t;

/**
 * @brief The scalar RHS
 **/
typedef struct {
	double         value;   /*!< The value of the RHS */
} psnl_cpu_rhs_scalar_t;

/**
 * @brief Describe an actual RHS 
 **/
typedef struct {
	/**
	 * @brief Read a single cell from the RHS
	 * @param pos The position
	 * @param lhs The RHS to read
	 * @param buf The result buffer
	 * @return status code
	 **/
	int (*read_func)(const int32_t* pos, const void* rhs, void* __restrict buf);
	/**
	 * @brief Dispose RHS cell
	 * @param rhs The RHS to dispose
	 * @return status code
	 **/
	int (*free_func)(const void* rhs);
	
	union {
		char          data[0];  /*!< The RHS data section */
		psnl_cpu_rhs_cont_t   cont;     /*!< The continuation RHS */
		psnl_cpu_rhs_field_t  field;    /*!< The field RHS */
		psnl_cpu_rhs_scalar_t scalar;   /*!< The scalar RHS */
	};
} psnl_cpu_rhs_t;

/**
 * @brief Init a field RHS
 * @param field The field to be the RHS object
 * @param buf The RHS buffer
 * @param elem_size The element size
 * @return status code
 **/
int psnl_cpu_rhs_init_field(const psnl_cpu_field_t* field, size_t elem_size, psnl_cpu_rhs_t* buf);

/**
 * @brief Init a continuation RHS
 * @param cont The continuation to be the RHS object
 * @param buf  The RHS buffer
 * @return status code
 **/
int psnl_cpu_rhs_init_cont(const psnl_cpu_field_cont_t* cont, psnl_cpu_rhs_t* buf);

/**
 * @brief Init a scalar RHS
 * @param value The actual value to set
 * @param buf The RHS buffer
 * @return status code
 **/
int psnl_cpu_rhs_init_scalar(double value, psnl_cpu_rhs_t* rhs);

/**
 * @brief Initialize a RHS
 * @param rhs The RHS description
 * @param buf The actual RHS object
 * @return status code
 **/
static inline int psnl_cpu_rhs_init(const psnl_cpu_rhs_desc_t* rhs, psnl_cpu_rhs_t* buf)
{
	switch(rhs->type)
	{
		case PSNL_CPU_RHS_SCALAR:
			return psnl_cpu_rhs_init_scalar(rhs->value.scalar, buf);
		case PSNL_CPU_RHS_FIELD:
			return psnl_cpu_rhs_init_field(rhs->value.field, rhs->elem_size, buf);
		case PSNL_CPU_RHS_CONT:
			return psnl_cpu_rhs_init_cont(rhs->value.cont, buf);
		default:
			ERROR_RETURN_LOG(int, "Invalid RHS Type");
	}
}

#endif /* __PSNL_CPU_RHS_H__ */
