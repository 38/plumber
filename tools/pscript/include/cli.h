/**
 * Copyright (C) 2017, Feng Liu
 **/
/**
 * @brief The interactive cli
 * @file pscript/include/cli.h
 **/
#ifndef __CLI_H__
#define __CLI_H__

#include <pss.h>

int pss_cli_interactive(uint32_t debug);
__attribute__((noreturn)) void properly_exit(int code);
void print_bt(pss_vm_backtrace_t*);
#endif
