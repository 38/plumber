/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The dimensional data
 * @file include/psnl/dim.h
 **/
#ifndef __PSNL_DIM_H__
#define __PSNL_DIM_H__

/**
 * @brief A dimensional data
 **/
typedef struct {
	uint32_t n_dim;      /*!< The numer of dimensions */
	uintpad_t __padding__[0];
	int32_t   dims[0][2]; /*!< The dimension data */
} psnl_dim_t;

/**
 * @brief Create a new dimension description
 * @param args The arguments
 * @return The dimensional data allocated on stack
 **/
#define PSNL_DIM_LOCAL_NEW(args...) ({\
	uint32_t tmp[][2] = {args};\
	psnl_dim_t* ret = alloca(sizeof(psnl_dim_t) + sizeof(tmp)); \
	if(NULL != ret)  \
	{\
		ret->n_dim = sizeof(tmp) /sizeof(tmp[0]);\
		memcpy(ret->dims, tmp, sizeof(tmp));\
	}\
	ret;\
})

/**
 * @brief Compute the offset for the cell in the given position
 * @param dim The dimensional data
 * @param pos The actual position
 * @return The offset
 **/
static inline uint32_t psnl_dim_get_offset(const psnl_dim_t* dim, const int32_t* pos)
{
	uint32_t stride = 1, ret = 0;

	uint32_t i;
	for(i = 0; i < dim->n_dim; i ++)
	{
		ret += stride * ((unsigned)(pos[i] - dim->dims[i][0]));
		stride *= (unsigned)(dim->dims[i][1] - dim->dims[i][0]);
	}

	return ret;
}

#endif /* __PSNL_DIM_H__ */
