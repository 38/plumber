/**
 * Copyright (C) 2017, Hao Hou
 **/
#ifndef __PSS_CLOSURE_H__
#define __PSS_CLOSURE_H__ 

/**
 * @brief The data type for a closure
 **/
typedef struct _pss_closure_t pss_closure_t;

/**
 * @brief The function used to initialize the global variables used by closure
 * @return status code
 **/
int pss_closure_init();

/**
 * @brief The module cleanup function
 * @return status code
 **/
int pss_closure_finalize();

/**
 * @brief The data strcture we used to create a new closure, this
 *        data structure will be provided to the mkval function of the closure
 *        operation
 **/
typedef struct {
	const pss_bytecode_segment_t* code;   /*!< The code we want to use for the closure */
	const pss_frame_t*            env;    /*!< The environment frame we want to use in the closure */
} pss_closure_creation_param_t;

/**
 * @brief Get a new stack frame forked from the closure's environment
 * @param closure The closure to operate
 * @return The newly create frame
 **/
pss_frame_t* pss_closure_get_frame(const pss_closure_t* closure);

/**
 * @brief Get the bytecode segment carried by this closure
 * @param closure The closure
 * @return The code segment
 **/
const pss_bytecode_segment_t* pss_closure_get_code(const pss_closure_t* closure);

#endif
