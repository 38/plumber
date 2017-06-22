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

#endif
