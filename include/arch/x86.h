
/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the CPU specified code on i386 CPU
 * file x86.h
 **/
#ifndef __PLUMBER_ARCH_X86_H__
#define __PLUMBER_ARCH_X86_H__

#	ifdef __i386__

/**
 * @breif the "atomic" increment operation on a 64 bit unsigned interger
 * @note the I386 implementation of the signle-writer-safe increment
 * @return nothing
 **/
static inline void arch_atomic_sw_increment_u64(uint64_t* var)
{
	__sync_fetch_and_add(var, 1);
}

/**
 * @brief increment operation on a 32 bit unsigned integer
 * @note the single-writer-safe increment for x86
 * @return nothing
 **/
static inline void arch_atomic_sw_increment_u32(uint32_t* var)
{
	(*var) ++;
}

/**
 * @biref the single-writer-safe assignment on a size_t variable
 * @param var the target address
 * @param val the value to assign
 * @return nothing
 **/
static inline void arch_atomic_sw_assignment_sz(size_t* var, size_t val)
{
	(*var) = val;
}

/**
 * @brief the signle-writer-safe assignment on a unsigned 32 bit integer
 * @param var the target address
 * @param val the value to assign
 * @return nothing
 **/
static inline void arch_atomic_sw_assignment_u32(uint32_t* var, uint32_t val)
{
	(*var) = val;
}

#	endif /* __i386 */

#endif /* __PLUMBER_ARCH_X64_H__ */
