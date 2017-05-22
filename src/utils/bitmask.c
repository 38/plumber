/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <string.h>

#include <error.h>

#include <utils/bitmask.h>
#include <utils/log.h>
#include <utils/static_assertion.h>

typedef uint64_t _node_t;

/**
 * @brief the actual data structure of a bitmask
 **/
struct _bitmask_t {
	size_t    size;              /*!< how many allocatable bits are mananged by this bit mask */
	size_t    bits;              /*!< how many bits in this mask */
	size_t    nodes;             /*!< number of nodes in the tree */
	size_t    last_level_bits;   /*!< how many bits is in the last level */
	_node_t  mask[0];           /*!< the actual mask data */
};
STATIC_ASSERTION_LAST(bitmask_t, mask);
STATIC_ASSERTION_SIZE(bitmask_t, mask, 0);

#define _NODE(x) ((_node_t)(x))

#define _BITS_PER_BYTE 8

#define _BPN (sizeof(_NODE(0)) * _BITS_PER_BYTE)

bitmask_t* bitmask_new(size_t size)
{
	size_t k;
	if(size == 0) ERROR_PTR_RETURN_LOG("Invalid arguments");
	/* We can prove that a heap in order K with N leaf node has N + Ceil((N-1) / (K-1)) Nodes */
	size_t bits = size + (size + _BPN - 3) / (_BPN - 1) - 1;
	size_t last_level_bits = bits;
	size_t nodes = (bits + _BPN - 1) / _BPN;
	for(k = _BPN; last_level_bits > k; last_level_bits -= k, k *= _BPN);
	LOG_DEBUG("The bitmask will have %zu bits include %zu in the last level", bits, last_level_bits);

	size_t bitmask_size = sizeof(bitmask_t) + sizeof(_node_t) * nodes;
	bitmask_t* ret = (bitmask_t*)calloc(1, bitmask_size);
	if(NULL == ret)
	{
		LOG_ERROR("cannot allocate memory for the new bit mask");
		return NULL;
	}

	ret->size = size;
	ret->bits = bits;
	ret->last_level_bits = last_level_bits;
	ret->nodes = nodes;
	ret->mask[nodes - 1] |= ~(( _NODE(1) << (bits % _BPN)) - 1);

	return ret;
}

int bitmask_clear(bitmask_t* bitmask)
{
	if(bitmask->nodes > 1)
	    memset(bitmask->mask, 0, (bitmask->nodes - 1) * sizeof(_node_t));
	bitmask->mask[bitmask->nodes - 1] = ~(( _NODE(1) << (bitmask->bits % _BPN)) - 1);
	return 0;
}

int bitmask_free(bitmask_t* bitmask)
{
	if(NULL == bitmask)
	{
		LOG_ERROR("Invalid arguments");
		return -1;
	}
	free(bitmask);

	return 0;
}

static inline void _bitmask_set(bitmask_t* bitmask, size_t idx, int val)
{
	size_t bitofs = (idx < bitmask->last_level_bits) ? (bitmask->bits - bitmask->last_level_bits + idx) :
	                                                   (bitmask->bits + idx - bitmask->size - bitmask->last_level_bits);
	size_t node = bitofs / _BPN;
	size_t bit  = bitofs % _BPN;

	if(val) bitmask->mask[node] |= (_NODE(1) << bit);
	else    bitmask->mask[node] &= ~(_NODE(1) << bit);

	for(;node > 0; node = (node - 1) / _BPN)
	{
		if((0u == ~bitmask->mask[node]))
		{
			if((bitmask->mask[(node - 1)/_BPN] & (_NODE(1) << ((node - 1) % _BPN)))) break;
			bitmask->mask[(node - 1)/_BPN] |= (_NODE(1) << ((node - 1) % _BPN));
		}
		else
		{
			if(((~bitmask->mask[(node - 1)/_BPN]) & (_NODE(1) << ((node - 1) % _BPN)))) break;
			bitmask->mask[(node - 1)/_BPN] &= ~(_NODE(1) << ((node - 1) %_BPN));
		}
	}
}

size_t bitmask_alloc(bitmask_t* bitmask)
{
	if(~bitmask->mask[0] == _NODE(0))
	    return (size_t)-1;

	size_t idx;
	for(idx = 0; idx * _BPN + 1 < bitmask->nodes;)
	{
		_node_t cur = bitmask->mask[idx];
		size_t nidx = idx * _BPN + 1;
		for(;cur&1;cur>>=1, nidx ++);
		if(nidx >= bitmask->nodes) break;
		idx = nidx;
	}

	size_t bit, ret;
	_node_t cur = bitmask->mask[idx];
	for(bit = 0; cur&1; cur >>= 1, bit ++);

	if(idx * _BPN + bit >= bitmask->bits - bitmask->last_level_bits)
	    ret = idx * _BPN + bit + bitmask->last_level_bits - bitmask->bits;
	else ret = idx * _BPN + bit + bitmask->last_level_bits +  bitmask->size - bitmask->bits;

	_bitmask_set(bitmask, ret, 1);

	return ret;
}

int bitmask_dealloc(bitmask_t* bitmask, size_t id)
{
	_bitmask_set(bitmask, id, 0);

	return 0;
}

int bitmask_full(const bitmask_t* bitmask)
{
	return 0u == ~bitmask->mask[0];
}

int bitmask_empty(const bitmask_t* bitmask)
{
	return 0u == bitmask->mask[0];
}
