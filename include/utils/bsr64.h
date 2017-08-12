/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The bsr64 utils
 * @file utils/bsr64.h
 **/

#ifndef __BSR64_H__
#define __BSR64_H__

/**
 * @brief convert the binary value to BSR string
 * @note This function only works on little endian CPUs, like X86
 * @todo make this work with little endian
 * @param bin The binary data
 * @param count The number of bytes
 * @param buffer The result buffer
 * @param bufsize The size of the result buffer
 * @return The string has been created, if the buffer is smaller than needed, return NULL
 **/
static inline const char* bsr64_from_bin(const void* bin, size_t count, char* buffer, size_t bufsize)
{
	const char* ret = buffer;
	static const char _val2ch[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
	const uint8_t* begin = (const uint8_t*)bin;
	const uint8_t* end   = begin + count;
	for(;end - begin >= 8 && bufsize > 0; begin += 6)
	{
		uint64_t v48 = 0xffff000000000000ull;
		v48 |= *(uint64_t*)begin;
		for(;v48 != 0xffff && bufsize > 0; v48 >>= 6, bufsize --)
		    *(buffer++) = _val2ch[v48&63];
	}

	uint32_t rem = 8;
	uint32_t val = 0;
	uint32_t required = 6;
	for(;begin < end && bufsize > 0;)
	{
		uint32_t to_read = required;
		if(to_read > rem) to_read = rem;
		val |= ((begin[0] & (((1u << to_read) - 1) << (8 - rem))) >> (8 - rem)) << (6 - required);
		rem -= to_read;
		required -= to_read;

		if(rem == 0) rem = 8, begin ++;
		if(required == 0) *(buffer++) = _val2ch[val], val = 0, required = 6;
	}

	if(required != 6 && bufsize > 0) *(buffer++) = _val2ch[val];

	if(bufsize == 0) return NULL;

	buffer[0] = 0;

	return ret;
}

/**
 * @brief Convert the binary-string representation to binary
 * @param bsr The BSR to convert
 * @param buf the buffer for the binary
 * @param bufsize The size of the buffer
 * @return status code
 **/
static inline size_t bsr64_to_bin(const char* bsr_begin, const char* bsr_end, void* buf, size_t bufsize)
{
	size_t ret = 0;
	static const uint8_t _ch2val[] = {
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 63, 64, 64,
		52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
		64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
		15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
		64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
	};

	uint8_t begin = 0;
	uint8_t* ptr = ((uint8_t*)buf);
	uint8_t  cur = 0;
	for(;bsr_begin != bsr_end && bufsize > 0; bsr_begin ++)
	{
		uint8_t val = _ch2val[*(uint8_t*)bsr_begin];

		if(val == 64) return 0;

		if(begin + 6 > 8)
		{
			if(begin < 8)
			{
				uint8_t delta = (uint8_t)((val & ((1u << (8 - begin)) - 1u)) << begin);
				cur |= delta;
			}
			*(ptr++) = cur;
			bufsize --;
			cur = (uint8_t)((val >> (8 - begin)));
			ret ++;
			begin = (uint8_t)(begin + 6u - 8u);
		}
		else
		{
			uint8_t delta = (uint8_t)(val << begin);
			cur |= delta;
			begin = (uint8_t)(begin + 6u);
		}
	}

	if(begin == 8 && bufsize > 0) *(ptr++) = cur, ret ++;

	if(bsr_begin != bsr_end) return 0;
	return ret;
}

#endif
