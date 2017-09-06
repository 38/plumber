
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

/**
 * @brief Switch current stack
 * @param baseaddr the stack base address
 * @param size the size of the stack
 * @param argc The number of arguments
 * @param argv The argument list
 * @return status code
 **/
__attribute__((noinline, used)) static int arch_switch_stack(void* baseaddr, size_t size, int (*main)(int, char **), int argc, char** argv)
{
	size_t offset = sizeof(void*);
	offset = offset + ((((~offset)&0xf) + 1)&0xf);
	void volatile ** stack = (void volatile **)(((uintptr_t)baseaddr) + size - offset);
#ifdef __clang__
	int rc;
	/* Because clang do not support named register variables, we need handle it differently */
	asm volatile (
	    "movl %%esp, (%0)\n"
		:
	    : "r"(stack)
		:
	);
#else
	register void* rsp   asm ("rsp");
	register int   rc    asm ("eax");
	stack[0] = rsp;
#endif
	asm volatile (
	    "movl %3, %%esp\n"
	    "\tpushl %1\n"
	    "\tpushl %0\n"
	    "\tcall  *%2\n"
	    "\tpopl  %%edx\n"
	    "\tpopl  %%edx\n"
	    "\tpop   %%edx\n"
	    "\tmovl %%edx, %%esp\n"
		:
	    : "r"(argc), "r"(argv), "r"(main), "r"(stack)
	    : "edx"
	);
#ifdef __clang__
	asm volatile (
	    "movl %%eax, %0"
	    : "=r"(rc)
		:
		:
	);
#endif
	return rc;
}

#	endif /* __i386 */

#endif /* __PLUMBER_ARCH_X64_H__ */
