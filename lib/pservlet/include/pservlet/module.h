/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the header for the module related APIs
 * @file pservlet/include/module.h
 **/
#include <error.h>
#ifndef __PSERVLET_MODULE_H__

/**
 * @brief require a service module function reference
 * @param mod_name the name of the module
 * @param func the function name
 * @return the pipe_t reference to the that function
 **/
pipe_t module_require_function(const char* mod_name, const char* func);


/**
 * @brief open a module and return the module code
 * @param path the path to the module
 * @return the module code
 **/
uint8_t module_open(const char* path);

/**
 * @brief get module sepecified opcode
 * @param path the module path
 * @param opcode the opcode
 * @return the module sepecified opcode
 **/
static inline uint32_t module_get_opcode(const char* path, uint32_t opcode)
{
	uint32_t mod = module_open(path);
	if(ERROR_CODE(uint8_t) == mod) return ERROR_CODE(uint32_t);

	return (mod << 24) | (opcode & (0xffffffffu >> 8));
}

#endif /* __PSERVLET_MODULE_H__ */
