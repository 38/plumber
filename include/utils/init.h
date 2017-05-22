/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the util for initialization/finalization a module
 * @file  init.h
 **/

#include <string.h>

#ifndef __INIT_H__
#define __INIT_H__

/** @brief a init vector entry, which contains a init function and a finalization function
 *  @note both init function and finalize function do not take any param and return < 0 only on error
 **/
typedef struct {
	int (*init)();      /*!< the function used for initialization */
	int (*finalize)();  /*!< the function used for finalization */
} init_vec_entry_t;


/**
 * @brief do initialization
 * @param sz the size of the vector
 * @param init_vec the initlaization vector
 * @return < 0 when it fails
 **/
int init_do_initialization(size_t sz, init_vec_entry_t* init_vec);

/**
 * @brief do finalization
 * @param sz the size of the vector
 * @param init_vec the intialization vector
 * @return < 0 when it fails
 **/
int init_do_finalization(size_t sz, init_vec_entry_t* init_vec);

/** @brief the macro that use to initalize the init vector
 *  @param name the name of the module
 *  @note this macro works based on the naming convention!
 **/
#define INIT_MODULE(name) {name##_init, name##_finalize}

/**
 * @brief define a private variable that contains a module list
 * @param name the name of the module list
 **/
#define INIT_VEC(name) static init_vec_entry_t name[]

/**
 * @brief do the initialization on a list
 **/
#define INIT_DO_INITIALIZATION(list) init_do_initialization(sizeof(list) / sizeof(*list), list)

/**
 * @brief do the finalization on a list
 **/
#define INIT_DO_FINALIZATION(list) init_do_finalization(sizeof(list) / sizeof(*list), list)

#endif /* __INIT_H__ */

