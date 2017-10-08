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

/**
 * @brief The main function of the interactive mode
 * @param debug If we want the debug info in the bytecode
 * @return status code
 **/
int cli_interactive(uint32_t debug);

/**
 * @brief Evaluate a code fagment and exit
 * @param code The code termiates with 0
 * @param debug If we want debug info in the bytecode
 * @return status code
 **/
int cli_eval(const char* code, uint32_t debug);
#endif
