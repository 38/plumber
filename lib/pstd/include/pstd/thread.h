/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The thread utils for pstd library
 * @file pstd/include/thread.h
 **/
#ifndef __PSTD_THREAD_H__

/**
 * @brief a thread local object
 **/
typedef struct _pstd_thread_local_t pstd_thread_local_t;

/**
 * @brief the allocator function used to allocate a new pointer to a new thread
 * @param tid the thread id
 * @param data the additional data passed to the allocator
 * @return the newly allocated memory NULL on error case
 **/
typedef void* (*pstd_thread_local_allocator_t)(uint32_t tid, const void* data);

/**
 * @brief the deallocator function used to free a pointer which allocated by the thread local allocator
 * @param mem the memory to free
 * @param data the additional data passed to the dealloctor
 * @return status code
 **/
typedef int (*pstd_thread_local_dealloctor_t)(void* mem, const void* data);

/**
 * @brief create a new thread local object
 * @param alloc the allocator
 * @param dealloc the dealloctor
 * @param data the additional data passed to the allocator and dealloctor
 * @return the newly created thread local object or NULL on error
 **/
pstd_thread_local_t* pstd_thread_local_new(pstd_thread_local_allocator_t alloc, pstd_thread_local_dealloctor_t dealloc, const void* data);

/**
 * @brief get the pointer which is allocated for current thread from the thread local object
 * @param local the thread local
 * @return the pointer for current thread or NULL on error
 **/
void* pstd_thread_local_get(pstd_thread_local_t* local);

/**
 * @brief dispose a used thread local
 * @param local the thread local
 * @return status code
 **/
int pstd_thread_local_free(pstd_thread_local_t* local);

#endif /* __PSTD_THREAD_H__ */
