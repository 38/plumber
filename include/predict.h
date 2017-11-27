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

#ifndef PREDICT_ASSERTION
#	define PREDICT_IMPOSSIBLE(expr) do{\
		if(expr) __builtin_unreachable();\
	} while(0)
#else
#	include <stdlib.h>
#	include <utils/log.h>
#	define PREDICT_IMPOSSIBLE(expr) do{\
		if(expr) \
		{\
			LOG_FATAL("Predict assertion failure: `"#expr"' should not be true"); \
			abort();\
		}\
		if(expr) __builtin_unreachable();\
	} while(0)
#endif

#endif /* __PLUMBER_PREDICT_H__ */
