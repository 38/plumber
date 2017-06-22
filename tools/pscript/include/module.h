/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The pscript module management utilities
 * @file  pscript/include/module.h
 **/
#ifndef __MODULE_H__
#define __MODULE_H__

/**
 * @brief Set the module search path
 * @param path The search path to add
 * @return status code
 **/
int module_set_search_path(char const* const* path);

/**
 * @brief Load a module from the given module name
 * @param name The name of the mdoule
 * @param dump_compiled Indicates if we want to dump the compiled bytecode to the psm file
 * @return status code
 **/
pss_bytecode_module_t* module_from_file(const char* name, int dump_compiled);

/**
 * @brief Load a module from the given buffer
 * @param code The buffer contains the code
 * @return The module compiled from the code buffer
 **/
pss_bytecode_module_t* module_from_buffer(const char* code);

#endif
