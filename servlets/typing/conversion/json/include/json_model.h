/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @file servlet/typing/conversion/json/include/json_model.h
 * @brief The JSON type model used by the JSON servlet
 **/

#ifndef __JSON_MODEL_H__
#define __JSON_MODEL_H__

/**
 * @brief the operation we should perform
 **/
typedef enum {
	JSON_MODEL_OPCODE_OPEN,     /*!< This means we should open an object for write */
	JSON_MODEL_OPCODE_OPEN_SUBS,/*!< This means we want to open a subscription */
	JSON_MODEL_OPCODE_CLOSE,    /*!< This means we should close the object because nothing to write */
	JSON_MODEL_OPCODE_WRITE     /*!< We need to write the primitive to the type */
} json_model_opcode_t;

/**
 * @brief The object mapping operations
 **/
typedef struct {
	json_model_opcode_t  opcode;        /*!< The operation code */
	char*                field;         /*!< Only used for opening a field: The field name we need to open */
	uint32_t             index;         /*!< The index used when we are opening an array list */
	pstd_type_accessor_t acc;  /*!< Only used for primitive field: The accessor we should use */
	size_t               size; /*!< Only used for primitive: The size of the data field */
	enum {
		JSON_MODEL_TYPE_SIGNED,              /*!< This is an integer */
		JSON_MODEL_TYPE_UNSIGNED,            /*!< This is a unsigned integer */
		JSON_MODEL_TYPE_FLOAT,               /*!< THis is a float point number */
		JSON_MODEL_TYPE_STRING               /*!< This is a string */
	}                    type; /*!< Only used for primitive: The type of this data field */
} json_model_op_t;

/**
 * @brief The output spec for each output ports
 **/
typedef struct {
	pipe_t              pipe;       /*!< The pipe that has the related type */
	char*               name;       /*!< The name of the pipe */
	uint32_t            cap;        /*!< The capacity of the operation array */
	uint32_t            nops;       /*!< The number of operations we need to be done for this type */
	json_model_op_t*    ops;        /*!< The operations we need to dump the JSON data to the plumber type */
} json_model_t;

/**
 * @brief create new type model
 * @param pipe_name The name of the pipe
 * @param type_name The type of the pipe
 * @param input Indicates if this is an input pipe
 * @param mem The memory we use to create the object
 * @param type_model The type model for this servlet
 * @note The json model assumes the callere will allocate the memory, and do not dispose the memory for the top level object
 *       All the functions of JSON model would assume that the libproto is properly initialized
 * @return The pointer to newly created
 **/
json_model_t* json_model_new(const char* pipe_name, const char* type_name, int input, pstd_type_model_t* type_model, void* mem);

/**
 * @brief dispose a used JSON model
 * @param model The model to dispose
 * @note This function do not free model pointer itself, it just cleanup all the internal reference
 *       As the json_model_new, this function also assumes the libproto has been properly initialized
 * @return status code
 **/
int json_model_free(json_model_t* model);

#endif /* __JSON_MODEL_H__ */
