/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The stack frame for the VM
 * @file pss/include/pss/frame.h
 **/
#ifndef __PSS_FRAME_H__
#define __PSS_FRAME_H__

/**
 * @brief The data structure for a frame
 **/
typedef struct _pss_frame_t pss_frame_t;

/**
 * @brief Create an frame
 * @param from If this param is not NULL, clone a frame 
 * @return The newly created frame
 **/
pss_frame_t* pss_frame_new(const pss_frame_t* from);

/**
 * @brief Dispose a used frame
 * @param frame The frame to dispose
 * @return status code
 **/
int pss_frame_free(pss_frame_t* frame);

/**
 * @brief Peek the register value of the given register
 * @param frame The frame we are working on
 * @param regid The register ID
 * @return The value 
 **/
pss_value_const_t pss_frame_reg_get(const pss_frame_t* frame, pss_bytecode_regid_t regid);

/**
 * @brief Set the value of the register
 * @param frame The frame to operate
 * @param regid The register ID
 * @param value The value to set
 * @return The status code
 **/
int pss_frame_reg_set(pss_frame_t* frame, pss_bytecode_regid_t regid, pss_value_t value);

/**
 * @brief Move the value of a register to another
 * @param frame The frame we are operating
 * @param from  The source register
 * @param dest  The destination register
 * @return The status code
 **/
int pss_frame_reg_move(pss_frame_t* frame, pss_bytecode_regid_t from, pss_bytecode_regid_t to);

#endif
