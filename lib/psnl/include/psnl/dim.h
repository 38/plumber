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
 * @brief Create a new dimension description buffer on stack
 * @param n The number of dimensions
 * @return The newly created dimensional data on stack
 * @note Every cell should be initailized to 0
 **/
#define PSNL_DIM_LOCAL_NEW_BUF(n) ({\
	psnl_dim_t* ret = (n < 512) ? alloca(psnl_dim_data_size_nd(n)) : NULL;\
	if(NULL != ret) \
	{\
		ret->n_dim = (uint32_t)(n);\
		memset(ret->dims, 0, sizeof((psnl_dim_data_size_nd(n))));\
	}\
	ret;\
})

/**
 * @brief Create a new dimension description on stack
 * @param args The arguments
 * @return The dimensional data allocated on stack
 **/
#define PSNL_DIM_LOCAL_NEW(args...) ({\
	int32_t tmp[][2] = {args};\
	psnl_dim_t* ret = alloca(sizeof(psnl_dim_t) + sizeof(tmp)); \
	if(NULL != ret)  \
	{\
		ret->n_dim = sizeof(tmp) /sizeof(tmp[0]);\
		memcpy(ret->dims, tmp, sizeof(tmp));\
	}\
	ret;\
})

/**
 * @brief Get the size of the dimension data for ND
 * @param n_dim The number of dimension
 * @return the number of bytes
 **/
static inline uint32_t psnl_dim_data_size_nd(uint32_t n_dim)
{
	return (uint32_t)(sizeof(psnl_dim_t) + 2 * sizeof(int32_t) * n_dim);
}

/**
 * @brief Get how many bytes the dimensional data uses
 * @param dim The dimensional data
 * @return The number of bytes
 **/
static inline uint32_t psnl_dim_data_size(const psnl_dim_t* dim)
{
	return psnl_dim_data_size_nd(dim->n_dim);
}

/**
 * @brief Compute how many cells in the space
 * @param dim The dimensional data
 * @return The size of the space
 **/
static inline uint32_t psnl_dim_space_size(const psnl_dim_t* dim)
{
	uint32_t ret = 1, i;
	for(i = 0; i < dim->n_dim; i ++)
		ret *= (uint32_t)(dim->dims[i][1] - dim->dims[i][0]);
	return ret;
}

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
	for(i = dim->n_dim - 1;; i --)
	{
		ret += stride * ((unsigned)(pos[i] - dim->dims[i][0]));
		stride *= (unsigned)(dim->dims[i][1] - dim->dims[i][0]);

		if(i == 0) break;
	}

	return ret;
}

#endif /* __PSNL_DIM_H__ */
