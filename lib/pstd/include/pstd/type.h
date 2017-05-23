/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The helper function for the pipe types
 * @file  pstd/include/type.h
 **/
#ifndef __PSTD_TYPE_H__
#define __PSTD_TYPE_H__

/**
 * @brief The servlet context object used to tack the type information for all the pipe
 *        for one servlet intance
 **/
typedef struct _pstd_type_model_t pstd_type_model_t;

/**
 * @brief The reference to an accessor in the type model
 **/
typedef uint32_t pstd_type_accessor_t;

/**
 * @brief Represent a instance of the servlet type context
 **/
typedef struct _pstd_type_instance_t pstd_type_instance_t;

/**
 * @brief The function used as the type assertion
 * @param type The type name
 * @param data The additional data to this assertion
 * @return status code
 **/
typedef int (*pstd_type_assertion_t)(const char* type, const void* data);

/**
 * @brief Create a new pipe type model object
 * @parm pipe The pipe we want to create the type info object for
 * @return the newly create pipe, NULL on error  case
 **/
pstd_type_model_t* pstd_type_model_new();

/**
 * @brief Dispose an used type model
 * @param model The type model to dispose
 * @return status code
 **/
int pstd_type_model_free(pstd_type_model_t* model);

/**
 * @param Register an accessor for the given pipe and field expression
 * @param model The typeinfo context
 * @param pipe The pipe descriptor
 * @param field_expr The field expression
 * @param result The result buffer
 * @return Status code
 **/
pstd_type_accessor_t pstd_type_model_get_accessor(pstd_type_model_t* model, pipe_t pipe, const char* field_expr);

/**
 * @brief Add a type assertion to the given pipe
 * @note  This function allows user add a type assertion, which checks if the type is expected
 * @param model     The type model
 * @param pipe      The target pipe descriptor
 * @param assertion The assertion function
 * @param data      The additional data we want to pass 
 * @return status code 
 **/
int pstd_type_model_assert(pstd_type_model_t* model, pipe_t pipe, pstd_type_assertion_t assertion, const void* data);

/**
 * @brief Comput the size of the type context instance for the given type model
 * @param model The input type model
 * @return The size or error code
 **/
size_t pstd_type_instance_size(const pstd_type_model_t* model);

/**
 * @brief Create a new type instance object 
 * @note  For each typed header IO, the servlet should create a type instance
 * @param model The type model we create the instance for
 * @param mem do not allocate memory, use the given pointer
 * @return The newly created type instance, NULL on error case
 **/
pstd_type_instance_t* pstd_type_instance_new(const pstd_type_model_t* model, void* mem);

/**
 * @brief Dispose a used type instance
 * @param inst The type instance to dispose
 * @return status code
 **/
int pstd_type_instance_free(pstd_type_instance_t* inst);

/**
 * @brief Read data from the given type accessor under the type context, this *must* be called inside exec task
 * @param inst     The type context isntance
 * @param accessor The accessor for the data we interseted in
 * @param buf      The buffer used to carry the result
 * @param bufsize  The size of the buffer
 * @return The number of bytes has been read to the buffer
 **/
size_t pstd_type_instance_read(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, void* buf, size_t bufsize);

/**
 * @brief Read typed primitive from the context and accessor
 * @param type The type we want to read
 * @parma inst The type context instance
 * @param accessor The accessor
 * @return The read result or error code
 **/
#define PSTD_TYPE_INST_READ_PRIMITIVE(type, inst, accessor) ({\
		type _resbuf;\
		{\
			pstd_type_instance_t* _inst = inst; \
			pstd_type_accessor_t _acc = accessor;\
			if(sizeof(_resbuf) != pstd_type_instance_read(_inst, _acc, &_resbuf, sizeof(_resbuf)))\
				_resbuf = ERROR_CODE(type);\
		}\
		_resbuf;\
	})

/**
 * @brief Write data to the pipe header, this *must* be called inside exec task
 * @param inst     The type context instance
 * @param accessor The accessor for the data we are interested in
 * @param buf      The data buffer we want to write
 * @param bufsize  The size of the buffer
 * @return Status code
 * @note Because this function will guarentee all the byte will be written to the buffer, so
 *       we do not provide the return value of how many bytes has been written. <br/>
 *       If the buffer size is larger than the size of the memory regoin the accessor refer to
 *       we just simple drop the extra data
 **/
int pstd_type_instance_write(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, const void* buf, size_t bufsize);

/**
 * @brief Write a primitive to the typed pipe header
 * @param inst     The type context instance
 * @param accessor The accessor for the data we are interested in
 * @param value    The value we want to write
 * @return status code
 **/
#define PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, accessor, value) ({\
		typeof(value) _buf = value;\
		pstd_type_instance_t* _inst = inst; \
		pstd_type_accessor_t _acc = accessor;\
		int rc = 0;\
		if(ERROR_CODE(int) == pstd_type_instance_write(_inst, _acc, &_buf, sizeof(_buf)))\
			rc = ERROR_CODE(int);\
		rc;\
	})


#endif /* __PSTD_PIPETYPE_H__ */
