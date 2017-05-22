/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The header file that used for the branch prediction
 * @file include/predict.h
 **/
#ifndef __PLUMBER_PREDICT_H__
#define __PLUMBER_PREDICT_H__

#define PREDICT_TRUE(expr) __builtin_expect(!!(expr), 1)

#define PREDICT_FALSE(expr) __builtin_expect(!!(expr), 0)

#endif /* __PLUMBER_PREDICT_H__ */
