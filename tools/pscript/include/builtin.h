/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The pscript builtin functions
 * @file pscript/include/builtin.h
 **/
#ifndef __BUILTIN_H__
#define __BUILTIN_H__

/**
 * @brief initialize the builtin functions
 * @param vm The PSS Virtual machine
 * @return status code
 **/
int builtin_init(pss_vm_t* vm);

/**
 * @brief Print the builtin functions
 * @param fp The file we want the doc to be print to
 * @param print_internals If we want to print internal functions
 * @param func If the function is given, we only print the doc for that func
 * @return nothing
 **/
void builtin_print_doc(FILE* fp, int print_internals, pss_value_builtin_t func);

/**
 * @brief Indicates if we currently have serivce running
 * @return result
 **/
int builtin_service_running();

#endif
