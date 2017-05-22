/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The language binding with C
 * @file proto/include/protoapi.h
 **/
#ifndef __PROTO_PROTOAPI_H__
#define __PROTO_PROTOAPI_H__
#include <stdint.h>
#include <stdlib.h>

/**
 * @brief query the protocol type API for the offset of the expression in the given type
 * @param type the name of the type
 * @param expr the field name expression
 * @param expected_size the size of expected result
 * @return the offset to the expression or error code
 **/
static inline uint32_t protoapi_offset_of(const char* type, const char* expr, uint32_t expected_size)
{
	uint32_t result_size, offset;
	offset = proto_db_type_offset(type, expr, &result_size);
	if(ERROR_CODE(uint32_t) == offset) return ERROR_CODE(uint32_t);
	if(result_size != expected_size) return ERROR_CODE(uint32_t);
	return offset;
}

/**
 * @brief query the protocol type api for the offset of the name expression in the given type name,
 *        store the result to the out_var. And the data type should be result_type, and the dimension
 *        information also should be given
 * @details For example, in order to get an array of float with 3 elements, we can use
 *          PROTOAPI_OFFSET_OF_VECTOR(float, field_offset, "Graphics/Vector3f", "data", 3)
 **/
#define PROTOAPI_OFFSET_OF_VECTOR(result_type, out_var, type_name, expr, dim...) do {\
	static uint32_t _dimension[] = {dim};\
	uint32_t expected_size = sizeof(result_type), i;\
	for(i = 0; i < sizeof(_dimension) / sizeof(_dimension[0]); i ++)\
	    expected_size *= _dimension[i];\
	out_var = protoapi_offset_of(type_name, expr, expected_size);\
} while(0)

/**
 * @brief this is the scalar version of PROTOAPI_OFFSET_OF_VECTOR
 **/
#define PROTOAPI_OFFSET_OF_SCALAR(result_type, out_var, type_name, expr) \
    PROTOAPI_OFFSET_OF_VECTOR(result_type, out_var, type_name, expr, 1)

#endif /* __PROTO_PROTOAPI_H__ */
