/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The RValue parser
 * @pfile pss/comp/value.h
 **/
#ifndef __PSS_COMP_VALUE_H__
#define __PSS_COMP_VALUE_H__


/**
 * @brief Represent an register
 **/
typedef struct {
	uint32_t               tmp:1;  /*!< indicates if this is a temporary register */
	pss_bytecode_regid_t   id;     /*!< The register ID */
} pss_comp_value_reg_t;

typedef enum {
	PSS_COMP_VALUE_KIND_REG,     /*!< The value lives in the register */
	PSS_COMP_VALUE_KIND_DICT,    /*!< The value lives in the dictionary */
	PSS_COMP_VALUE_KIND_GLOBAL   /*!< The value lives in the global storage */
} pss_comp_value_kind_t;

/**
 * @brief Represent a value, either lvalue or rvalue
 **/
typedef struct {
	pss_comp_value_kind_t kind;    /*!< What kind of value it is */
	pss_comp_value_reg_t  regs[2]; /*!< The registers that carries this value */ 
} pss_comp_value_t;


/**
 * @brief Parse a value
 * @param comp The compilter instance
 * @param result The compilation result
 * @return s tatus code
 **/
int pss_comp_value_parse(pss_comp_t* comp, pss_comp_value_t* result);

/**
 * @brief Check if this value is a L-value
 * @param value the value to check
 * @return check result
 **/
static inline int pss_comp_value_is_lvalue(const pss_comp_value_t* value)
{
	return value->kind == PSS_COMP_VALUE_KIND_DICT ||
		   value->kind == PSS_COMP_VALUE_KIND_GLOBAL ||
		   (value->kind == PSS_COMP_VALUE_KIND_REG &&
			value->regs[0].tmp == 0);
}

/**
 * @brief Simplify a value.
 * @detail This means if the value is a L-Value, dereference it and put it to a tmp register
 * @param comp The compiler instance
 * @param value The value
 * @return status code
 **/
int pss_comp_value_simplify(pss_comp_t* comp, pss_comp_value_t* value);

/**
 * @brief Release the used value
 * @param comp The compiler instance
 * @param value The value to release
 * @return status code
 **/
int pss_comp_value_release(pss_comp_t* comp, pss_comp_value_t* value);

#endif
