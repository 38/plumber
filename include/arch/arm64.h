
/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the CPU specified code on 64bit ARM CPU
 * file arm64.h
 **/
#ifndef __PLUMBER_ARCH_ARM64_H__
#define __PLUMBER_ARCH_ARM64_H__

#	ifdef __arm64__

/**
 * @breif the "atomic" increment operation on a 64 bit unsigned interger
 * @note the I386 implementation of the signle-writer-safe increment
 * @return nothing
 **/
static inline void arch_atomic_sw_increment_u64(uint64_t* var)
{
	(*var) ++;
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

#if 0
/**
 * @brief Switch current stack
 * @param baseaddr the stack base address
 * @param size the size of the stack
 * @param argc The number of arguments
 * @param argv The argument list
 * @return status code
 **/
__attribute__((noinline, used, error("fixme: stack switching for ARM32 haven't been implemented")))
static int arch_switch_stack(void* baseaddr, size_t size, int (*main)(int, char **), int argc, char** argv)
{
	(void)baseaddr;
	(void)size;
	(void)main;
	(void)argc;
	(void)argv;
	return 0;
}
#endif

#	endif /* __arm64__ */

#endif /* __PLUMBER_ARCH_ARM64_H__ */
