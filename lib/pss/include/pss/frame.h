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

/**
 * @brief This function convert the serial numer to register ID
 * @details The way we store registers in a frame is a copy-on-write tree. On the tree, different
 *          register have different length of path. 
 *          The serial number is the numer assigned to register from the root level to the leaf leave,
 *          from left side to the right side.
 *          This is the function maps the serial number to register ID.
 *          This is useful when we manage the registers for code generation, because we always want to
 *          use the regsiter that is near from the root
 * @param serial The serial id
 * @return The register id
 **/
static inline pss_bytecode_regid_t pss_frame_serial_to_regid(pss_bytecode_regid_t serial)
{
	uint32_t log2 = 0, val = 1 + (uint32_t)serial;
	for(;val > 1; val >>= 1, log2 ++);

	pss_bytecode_regid_t suffix = (pss_bytecode_regid_t)(0x7fff >> log2);
	pss_bytecode_regid_t prefix = (pss_bytecode_regid_t)(serial << (16 - log2));

	return prefix | suffix;
}

#endif
