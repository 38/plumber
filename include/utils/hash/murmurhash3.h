/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The implementation of Murmurhash 3
 * @file utils/hash/murmurhash3.h
 **/
#ifndef __PLUMBER_UTILS_HASH_MURMURHASH3_H__
#define __PLUMBER_UTILS_HASH_MURMURHASH3_H__

static inline uint64_t _murmurhash3_rotl64(uint64_t x, int8_t r)
{
	return (x << r) | (x >> (64 - r));
}

static inline uint64_t _murmurhash3_fmix64(uint64_t k)
{
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdull;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53ull;
	k ^= k >> 33;

	return k;
}

/**
 * @brief currently we use the murmur hash 3 as the hash function
 * @note  see the origin implementation from http://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp
 * @param key the key to hash
 * @param len the length of the key
 * @param out the output array
 * @param hash_seed The seed of the hash
 * @return nothing
 **/
static inline void murmurhash3_128(const void* key, const size_t len, uint32_t hash_seed, uint64_t out[2])
{
	size_t tail_size = len % 16, i;
	const uint8_t *tail = ((const uint8_t*)key) + (len - tail_size);
	const uint64_t *head = (const uint64_t*)key;

	out[0] = out[1] = hash_seed;

	const uint64_t c1 = 0x87c37b91114253d5ull;
	const uint64_t c2 = 0x4cf5ad432745937full;

	for(i = 0; i < len / 16; i ++)
	{
		uint64_t k1 = head[i * 2];
		uint64_t k2 = head[i * 2 + 1];

		k1 *= c1;
		k1 = _murmurhash3_rotl64(k1, 31);
		k1 *= c2;
		out[0] ^= k1;
		out[0] = _murmurhash3_rotl64(out[0], 27);
		out[0] += out[1];
		out[0] = out[0] * 5 + 0x52dce729;

		k2 *= c2;
		k2 = _murmurhash3_rotl64(k2, 33);
		k2 *= c1;
		out[1] ^= k2;
		out[1] = _murmurhash3_rotl64(out[1], 31);
		out[1] += out[0];
		out[1] = out[1] * 5 + 0x38495ab5;
	}

	uint64_t k1 = 0;
	uint64_t k2 = 0;

	switch(tail_size)
	{
		case 15: k2 ^= ((uint64_t)tail[14]) << 48;
		case 14: k2 ^= ((uint64_t)tail[13]) << 40;
		case 13: k2 ^= ((uint64_t)tail[12]) << 32;
		case 12: k2 ^= ((uint64_t)tail[11]) << 24;
		case 11: k2 ^= ((uint64_t)tail[10]) << 16;
		case 10: k2 ^= ((uint64_t)tail[9]) << 8;
		case 9: k2 ^= ((uint64_t)tail[8]) << 0;
		    k2 *= c2;
		    k2 = _murmurhash3_rotl64(k2, 33);
		    k2 *= c1;
		    out[1] ^= k2;
		case 8: k1 ^= ((uint64_t)tail[7]) << 56;
		case 7: k1 ^= ((uint64_t)tail[6]) << 48;
		case 6: k1 ^= ((uint64_t)tail[5]) << 40;
		case 5: k1 ^= ((uint64_t)tail[4]) << 32;
		case 4: k1 ^= ((uint64_t)tail[3]) << 24;
		case 3: k1 ^= ((uint64_t)tail[2]) << 16;
		case 2: k1 ^= ((uint64_t)tail[1]) << 8;
		case 1: k1 ^= ((uint64_t)tail[0]) << 0;
		    k1 *= c1;
		    k1 = _murmurhash3_rotl64(k1, 31);
		    k1 *= c2;
		    out[0] ^= k1;
	}

	out[0] ^= len;
	out[1] ^= len;

	out[0] += out[1];
	out[1] += out[0];

	out[0] = _murmurhash3_fmix64(out[0]);
	out[1] = _murmurhash3_fmix64(out[1]);

	out[0] += out[1];
	out[1] += out[0];
}

#endif /* __PLUMBER_UTILS_HASH_MURMURHASH3_H__ */
