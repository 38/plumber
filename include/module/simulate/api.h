/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the API header for Event Simulation Module
 * @brief file simulate/api.h
 **/
#ifndef __MODULE_SIMULATE_API_H__
#define __MODULE_SIMULATE_API_H__

/**
 * @brief The module prefix for the event simulators
 **/
#define MODULE_SIMULATE_PREFIX "pipe.simulate"

/**
 * @brief The raw control opcode that gets a event lavel
 **/
#define MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL_RAW 0x0

#ifdef __PSERVLET__

PIPE_DEFINE_MOD_OPCODE_GETTER(MODULE_SIMULATE_PREFIX, MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL_RAW);

#define MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL PIPE_MOD_OPCODE(MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL_RAW)

#else

#define MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL MODULE_SIMULATE_CNTL_OPCODE_GET_LABEL_RAW

#endif

#endif /* __MODULE_SIMULATE_API_H__ */
