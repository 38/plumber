/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The request local scope
 * @details This is the wrapper function for the request local scope infrastructure, which manages the
 *          pointers shared among the tasks of the same request
 * @file pstd/include/scope.h
 **/
#ifndef __PSTD_SCOPE_H__
#define __PSTD_SCOPE_H__

/**
 * @brief add a new pointer to the scope managed pointer infrastructure
 * @details Once this function gets called, the memory will be assigned with an integer token, then the servlet
 *          will be able to write the token to pipe, and the downstream servlet will be able to get the pointer
 *          by the token in the pipe. <br/>
 *          The memory will be automatically disposed after the request is done. <br/>
 *          This function will take the ownership of the pointer mem, so do not dispose the pointer once the function
 *          returns successfully
 * @param entity the scope entity to add
 * @note  the entity parameter do not pass the ownership of the entity, however, entity->data will be taken if
 *        the function retuens successfully
 * @return  the token for the pointer or error code
 **/
scope_token_t pstd_scope_add(const scope_entity_t* entity);

/**
 * @brief Copy the existing token
 * @details this function will make a copy of the existing memory by calling its copy callback, and then assign it with
 *         a new scope token. This should be used if the shared memory needs to be changed, because we want to make sure
 *         that the result shouldn't be related to the servlet execution order. However, if there are two servlets needs
 *         to change the same pointer, it's possible that the previous one will be overridden by the latter one. So we
 *         need to make sure each of the servlet make their own copy before the change. <br/>
 *         And this guareentee the reuslt is not related to the execution order
 * @param  token the token to copy
 * @param  resbuf the result buffer used to return the pointer after the copy
 * @return the token for the copied pointer or error code
 **/
scope_token_t pstd_scope_copy(scope_token_t token, void** resbuf);

/**
 * @brief get the underlying pointer from the scope token
 * @param token the scope token
 * @return the pointer, NULL on error case
 **/
const void* pstd_scope_get(scope_token_t token);

#endif /* __PSTD_SCOPE_H__ */
