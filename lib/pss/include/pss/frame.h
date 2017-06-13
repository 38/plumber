/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The stack frame for the VM
 * @file pss/include/pss/frame.h
 **/
#ifndef __PSS_FRAME_H__
#define __PSS_FRAME_H__

typedef struct _pss_frame_t pss_frame_t;

pss_frame_t* pss_frame_new();

int pss_frame_free();

pss_frame_t* pss_frame_copy(pss_frame_t* frame);

pss_frame_get_register(const pss_frame_t* frame, pss_bytecode_regid_t regid);

#endif
