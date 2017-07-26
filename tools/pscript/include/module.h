/**
 * Copyright (C) 2017, Hao Hou
 * Copyright (C) 2017, Feng Liu
 **/
/**
 * @brief The pscript module management utilities
 * @file  pscript/include/module.h
 **/
#ifndef __MODULE_H__
#define __MODULE_H__

/**
 * @brief The set the module search pathes
 * @param paths the module search paths
 * @return status code
 **/
int module_set_search_path(char const* const* paths);

/**
 * @brief Load a module from the given module name
 * @param name The name of the mdoule
 * @param dump_compiled Indicates if we want to dump the compiled bytecode to the psm file
 * @return module
 **/
pss_bytecode_module_t* module_from_file(const char* name, int load_compiled, int dump_compiled, int debug, const char* compiled_output);

/**
 * @brief Load a module from the given buffer
 * @param code The buffer contains the code
 * @param code_size The size of the code
 * @return The module compiled from the code buffer
 **/
pss_bytecode_module_t* module_from_buffer(const char* code, uint32_t code_size, uint32_t debug);

/**
 * @brief Check if the module has been previously loaded
 * @param name The name of the module
 * @return The check result or status code
 **/
int module_is_loaded(const char* name);

/**
 * @brief Unload all modules that has been loaded
 * @return status code
 **/
int module_unload_all();

#endif
